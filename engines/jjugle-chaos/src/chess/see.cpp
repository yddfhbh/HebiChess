#include "chess/see.hpp"
#include "chess/search_profile.hpp"

#include <algorithm>
#include <optional>

#include "chess/eval.hpp"

namespace hebichess {
namespace {

Square at(int file, int rank) noexcept {
  if (file < 0 || file >= 8 || rank < 0 || rank >= 8) return {};
  return Square::from_file_rank(static_cast<std::uint8_t>(file),
                                static_cast<std::uint8_t>(rank));
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
  const auto consider = [&](Square from, PieceType expected_type = PieceType::None) {
    if (!from.is_valid()) return;
    const Piece attacker = board.piece_at(from);
    if (attacker.is_empty() || attacker.color != side ||
        (expected_type != PieceType::None && attacker.type != expected_type)) return;
    const int value = piece_value(attacker.type);
    if (value > best_value ||
        (best.has_value() && value == best_value && from.index() >= best->from.index()))
      return;
    const bool promotes = attacker.type == PieceType::Pawn &&
                          (target.rank() == 0 || target.rank() == 7);
    const Move move{from, target, promotes ? PieceType::Queen : PieceType::None,
                    promotes ? MoveFlag::PromotionCapture : MoveFlag::Capture};
    if (!legal_capture(board, move, side)) return;
    best = move;
    best_value = value;
  };

  // Work outwards from the target rather than testing all 64 origin squares.
  // This is equivalent to finding the first legal attacker of minimum value;
  // ties keep the original lowest-square-index ordering.
  const int file = target.file();
  const int rank = target.rank();
  const int pawn_rank = rank + (side == Color::White ? -1 : 1);
  consider(at(file - 1, pawn_rank), PieceType::Pawn);
  consider(at(file + 1, pawn_rank), PieceType::Pawn);
  if (best_value == piece_value(PieceType::Pawn)) return best;

  static constexpr int knight_steps[8][2] = {
      {1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
  for (const auto& step : knight_steps)
    consider(at(file + step[0], rank + step[1]), PieceType::Knight);

  static constexpr int directions[8][2] = {
      {1, 0}, {-1, 0}, {0, 1}, {0, -1},
      {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
  for (int ray = 0; ray < 8; ++ray) {
    for (int f = file + directions[ray][0], r = rank + directions[ray][1];
         f >= 0 && f < 8 && r >= 0 && r < 8;
         f += directions[ray][0], r += directions[ray][1]) {
      const Square from = at(f, r);
      const Piece piece = board.piece_at(from);
      if (piece.is_empty()) continue;
      if (piece.color == side &&
          (piece.type == PieceType::Queen ||
           piece.type == (ray < 4 ? PieceType::Rook : PieceType::Bishop)))
        consider(from);
      break;
    }
  }
  for (const auto& step : directions)
    consider(at(file + step[0], rank + step[1]), PieceType::King);
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
  SampledProfileTimer profile_timer(ProfileMetric::See, 256);
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
  // Fast path: if the just-captured square has no geometric recapturer,
  // even a pinned/illegal recapture is impossible. Avoid the SEE exchange
  // tree and its repeated whole-board attacker scans in this common case.
  // A positive attack check is only a hint: the legal SEE remains authoritative.
  if (!after.is_square_attacked(move.to, opposite(moving.color))) return gain;
  gain -= reply_exchange(after, move.to);
  return gain;
}

bool see_ge(const Board& board, const Move& move, int threshold) noexcept {
  return static_exchange_eval(board, move) >= threshold;
}

}  // namespace hebichess
