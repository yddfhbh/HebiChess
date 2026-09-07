#include "chess/see.hpp"

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
  return static_exchange_eval(board, move) >= threshold;
}

}  // namespace hebichess
