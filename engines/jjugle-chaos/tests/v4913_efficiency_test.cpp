#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "chess/board.hpp"
#include "chess/movegen.hpp"
#include "chess/see.hpp"
#include "chess/time_management.hpp"
#include "chess/uci.hpp"
#include "chess/uci_engine.hpp"

namespace legacy_see {
using namespace hebichess;
namespace {

Square at(int file, int rank) noexcept {
  if (file < 0 || file >= 8 || rank < 0 || rank >= 8) return {};
  return Square::from_file_rank(static_cast<std::uint8_t>(file),
                                static_cast<std::uint8_t>(rank));
}

bool attacks_target(const Board& board, Square from, Square target) noexcept {
  const Piece piece = board.piece_at(from);
  const int df = static_cast<int>(target.file()) - from.file();
  const int dr = static_cast<int>(target.rank()) - from.rank();
  if (piece.type == PieceType::Pawn)
    return dr == (piece.color == Color::White ? 1 : -1) && (df == 1 || df == -1);
  if (piece.type == PieceType::Knight) return df * df + dr * dr == 5;
  if (piece.type == PieceType::King)
    return std::max(std::abs(df), std::abs(dr)) == 1;

  const bool diagonal = df != 0 && dr != 0 && std::abs(df) == std::abs(dr);
  const bool straight = (df == 0) != (dr == 0);
  if ((piece.type == PieceType::Bishop && !diagonal) ||
      (piece.type == PieceType::Rook && !straight) ||
      (piece.type == PieceType::Queen && !diagonal && !straight)) return false;

  const int sf = df == 0 ? 0 : (df > 0 ? 1 : -1);
  const int sr = dr == 0 ? 0 : (dr > 0 ? 1 : -1);
  for (int file = from.file() + sf, rank = from.rank() + sr;
       file != target.file() || rank != target.rank(); file += sf, rank += sr) {
    if (!board.piece_at(at(file, rank)).is_empty()) return false;
  }
  return true;
}

int promotion_gain(const Piece& piece, const Move& move) noexcept {
  return move.is_promotion() ? piece_value(move.promotion) - piece_value(piece.type) : 0;
}

bool legal_capture(Board& board, const Move& move, Color side) noexcept {
  const UndoState undo = board.make_move(move);
  const Square king = board.find_king(side);
  const bool legal = !king.is_valid() || !board.is_square_attacked(king, opposite(side));
  board.unmake_move(move, undo);
  return legal;
}

std::optional<Move> least_valuable_attacker(Board& board, Square target,
                                             Color side) noexcept {
  const Piece victim = board.piece_at(target);
  if (victim.is_empty() || victim.color == side) return std::nullopt;

  std::optional<Move> best;
  int best_value = 100000;
  for (std::uint8_t index = 0; index < Square::kSquareCount; ++index) {
    const Square from = Square::from_index(index);
    const Piece attacker = board.piece_at(from);
    if (attacker.is_empty() || attacker.color != side ||
        !attacks_target(board, from, target)) continue;
    const bool promotes = attacker.type == PieceType::Pawn &&
                          (target.rank() == 0 || target.rank() == 7);
    const Move move{from, target, promotes ? PieceType::Queen : PieceType::None,
                    promotes ? MoveFlag::PromotionCapture : MoveFlag::Capture};
    if (!legal_capture(board, move, side)) continue;
    const int value = piece_value(attacker.type);
    if (!best.has_value() || value < best_value) {
      best = move;
      best_value = value;
    }
  }
  return best;
}

int reply_exchange(Board& board, Square target) noexcept {
  const Color side = board.side_to_move();
  const auto attacker = least_valuable_attacker(board, target, side);
  if (!attacker.has_value()) return 0;

  const Piece moving = board.piece_at(attacker->from);
  const Piece victim = board.piece_at(target);
  const int gain = piece_value(victim.type) + promotion_gain(moving, *attacker);
  const UndoState undo = board.make_move(*attacker);
  const int reply = reply_exchange(board, target);
  board.unmake_move(*attacker, undo);
  return std::max(0, gain - reply);
}

}  // namespace

int static_exchange_eval(const Board& board, const Move& move) noexcept {
#if HEBICHESS_SEARCH_PROFILE
  // None (reference implementation)
#endif
  const Piece moving = board.piece_at(move.from);
  if (moving.is_empty()) return 0;

  const bool capture = move.flag == MoveFlag::Capture ||
                       move.flag == MoveFlag::PromotionCapture ||
                       move.flag == MoveFlag::EnPassant;
  if (!capture && !move.is_promotion()) return 0;

  Piece victim = board.piece_at(move.to);
  if (move.flag == MoveFlag::EnPassant) victim = {PieceType::Pawn, opposite(moving.color)};
  int gain = piece_value(victim.type) + promotion_gain(moving, move);

  Board after = board;
  after.make_move(move);
  gain -= reply_exchange(after, move.to);
  return gain;
}

bool see_ge(const Board& board, const Move& move, int threshold) noexcept {
  return legacy_see::static_exchange_eval(board, move) >= threshold;
}

} // namespace legacy_see

using namespace hebichess;
void require(bool condition, const char* message) {
  if (!condition) { std::cerr << "V4913_FAIL=" << message << '\n'; std::exit(1); }
}

void compare_on_board(Board& board, int& samples) {
  const auto moves = generate_legal_moves(board);
  for (const auto& move : moves) {
    if (move.flag != MoveFlag::Capture &&
        move.flag != MoveFlag::PromotionCapture &&
        move.flag != MoveFlag::EnPassant && !move.is_promotion()) continue;
    const int expected = legacy_see::static_exchange_eval(board, move);
    const int actual = static_exchange_eval(board, move);
    if (expected != actual) {
      std::cerr << "V4913_SEE_MISMATCH=" << board.to_fen() << " "
                << move_to_uci(move) << " expected=" << expected
                << " actual=" << actual << '\n';
      std::exit(1);
    }
    ++samples;
  }
}

void see_equivalence() {
  int samples = 0;
  std::uint32_t state = 0x95cb5d91u;
  // Multiple independently started playouts exercise exchange sequences,
  // x-rays, pins, recaptures and both sides to move.
  for (int game = 0; game < 16; ++game) {
    Board board = Board::initial();
    for (int ply = 0; ply < 110; ++ply) {
      compare_on_board(board, samples);
      auto legal = generate_legal_moves(board);
      if (legal.empty()) break;
      // Prefer captures one-third of the time to build tactical fixtures.
      state = state * 1664525u + 1013904223u;
      std::vector<Move> captures;
      for (const Move& m : legal)
        if (m.flag == MoveFlag::Capture || m.flag == MoveFlag::PromotionCapture ||
            m.flag == MoveFlag::EnPassant) captures.push_back(m);
      const auto& pool = (!captures.empty() && (state % 3) == 0) ? captures : legal;
      const Move m = pool[state % pool.size()];
      board.make_move(m);
    }
  }
  for (const char* fen : {
       "8/P6k/8/8/8/8/6Kp/8 w - - 0 1",
       "8/P6k/8/8/8/8/6Kp/8 b - - 0 1",
       "3b1rk1/1p1n1ppp/p3p1b1/1q1pP1B1/1P1P4/4NN1P/3Q1PP1/2R3K1 b - - 11 27"}) {
    auto board = Board::from_fen(fen);
    require(board.has_value(), "fixture parses");
    compare_on_board(*board, samples);
  }
  require(samples >= 300, "SEE capture coverage");
  std::cout << "V4913_SEE_EQUIVALENCE=PASS samples=" << samples << '\n';
}

void clock_scaling() {
  auto budget = allocate_time_budget(99000, 1000, false, 27);
  auto faster = scale_time_budget(budget, 75);
  require(faster.soft_ms < budget.soft_ms && faster.hard_ms < budget.hard_ms,
          "default clock usage cuts latency");
  require(faster.hard_ms >= faster.soft_ms && faster.soft_ms >= 1,
          "clock scaling preserves valid time windows");
  auto original = scale_time_budget(budget, 100);
  require(original.soft_ms == budget.soft_ms && original.hard_ms == budget.hard_ms,
          "legacy 100 percent budget remains available");
  auto low = scale_time_budget(allocate_time_budget(1800, 0, false, 45), 75);
  require(low.hard_ms < 1800 && low.soft_ms > 0,
          "very low clock reserve stays protected");
  std::vector<std::string> lines;
  UciEngine uci([&](const std::string& line) { lines.push_back(line); });
  uci.send_command("uci");
  require(std::find(lines.begin(), lines.end(),
       "option name ThinkTimePercent type spin default 75 min 35 max 125") != lines.end(),
       "UCI exposes clock rate");
  lines.clear();
  uci.send_command("setoption name ThinkTimePercent value 100");
  require(std::find(lines.begin(), lines.end(), "info string ThinkTimePercent 100") != lines.end(),
          "UCI accepts legacy budget");
  lines.clear();
  uci.send_command("setoption name ThinkTimePercent value 130");
  require(std::find(lines.begin(), lines.end(), "info string error invalid ThinkTimePercent") != lines.end(),
          "UCI rejects invalid rate");
  std::cout << "V4913_CLOCK_SCALING=PASS\n";
}

int main() {
  see_equivalence();
  clock_scaling();
  std::cout << "V4913_SEARCH_EFFICIENCY=PASS\n";
}
