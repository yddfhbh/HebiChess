#include "chess/eval.hpp"

#include <array>
#include <cstdlib>

#include "chess/movegen.hpp"

namespace hebichess {
namespace {

Square at(int file, int rank) noexcept {
  if (file < 0 || file >= 8 || rank < 0 || rank >= 8) return {};
  return Square::from_file_rank(static_cast<std::uint8_t>(file),
                                static_cast<std::uint8_t>(rank));
}

bool attacks(const Board& board, Square from, Square target) noexcept {
  const Piece piece = board.piece_at(from);
  const int df = static_cast<int>(target.file()) - from.file();
  const int dr = static_cast<int>(target.rank()) - from.rank();
  if (piece.type == PieceType::Pawn)
    return dr == (piece.color == Color::White ? 1 : -1) && (df == 1 || df == -1);
  if (piece.type == PieceType::Knight)
    return (df * df + dr * dr) == 5;
  if (piece.type == PieceType::King)
    return df >= -1 && df <= 1 && dr >= -1 && dr <= 1 && (df != 0 || dr != 0);
  const bool diagonal = df != 0 && dr != 0 && (df < 0 ? -df : df) == (dr < 0 ? -dr : dr);
  const bool straight = (df == 0) != (dr == 0);
  if ((piece.type == PieceType::Bishop && !diagonal) ||
      (piece.type == PieceType::Rook && !straight) ||
      (piece.type == PieceType::Queen && !diagonal && !straight)) return false;
  const int sf = df == 0 ? 0 : (df > 0 ? 1 : -1);
  const int sr = dr == 0 ? 0 : (dr > 0 ? 1 : -1);
  for (int f = from.file() + sf, r = from.rank() + sr; f != target.file() || r != target.rank(); f += sf, r += sr)
    if (!board.piece_at(at(f, r)).is_empty()) return false;
  return true;
}

int white_score(int white, int black, Color perspective) noexcept {
  const int score = white - black;
  return perspective == Color::White ? score : -score;
}

int pressure_for(const Board& board, Color attacker) noexcept {
  const Square king = board.find_king(opposite(attacker));
  if (!king.is_valid()) return 0;
  int pressure = 0;
  int attackers = 0;
  for (std::uint8_t i = 0; i < Square::kSquareCount; ++i) {
    const Square from = Square::from_index(i);
    const Piece piece = board.piece_at(from);
    if (piece.is_empty() || piece.color != attacker) continue;
    for (int df = -1; df <= 1; ++df) for (int dr = -1; dr <= 1; ++dr) {
      const Square zone = at(king.file() + df, king.rank() + dr);
      if (!zone.is_valid() || !attacks(board, from, zone)) continue;
      ++attackers;
      pressure += piece.type == PieceType::Pawn ? 2 : piece.type == PieceType::Knight ? 6 :
                  piece.type == PieceType::Bishop ? 5 : piece.type == PieceType::Rook ? 7 :
                  piece.type == PieceType::Queen ? 10 : 3;
      break;
    }
  }
  return pressure + (attackers >= 2 ? attackers * 2 : 0) + (attackers >= 3 ? 5 : 0);
}

}  // namespace

int piece_value(PieceType type) noexcept {
  switch (type) {
    case PieceType::Pawn: return 100;
    case PieceType::Knight: return 320;
    case PieceType::Bishop: return 330;
    case PieceType::Rook: return 500;
    case PieceType::Queen: return 900;
    default: return 0;
  }
}

int evaluate_material(const Board& board, Color perspective) noexcept {
  int white = 0, black = 0;
  for (const Piece piece : board.squares()) {
    if (piece.color == Color::White) white += piece_value(piece.type);
    else black += piece_value(piece.type);
  }
  return white_score(white, black, perspective);
}

int evaluate_piece_activity(const Board& board, Color perspective) noexcept {
  int white = 0, black = 0;
  for (std::uint8_t i = 0; i < Square::kSquareCount; ++i) {
    const Square square = Square::from_index(i);
    const Piece piece = board.piece_at(square);
    if (piece.is_empty() || piece.type == PieceType::King || piece.type == PieceType::Pawn) continue;
    const int center = 3 - std::abs(3 - static_cast<int>(square.file())) +
                       3 - std::abs(3 - static_cast<int>(square.rank()));
    const int bonus = piece.type == PieceType::Knight ? center * 2 : center;
    (piece.color == Color::White ? white : black) += bonus;
  }
  return white_score(white, black, perspective);
}

int evaluate_mobility(const Board& board, Color perspective) noexcept {
  int white = 0, black = 0;
  for (const Color color : {Color::White, Color::Black}) {
    Board copy = board;
    copy.set_side_to_move(color);
    const int count = static_cast<int>(generate_pseudo_legal_moves(copy).size());
    (color == Color::White ? white : black) = count;
  }
  return white_score(white, black, perspective);
}

int evaluate_king_safety(const Board& board, Color perspective) noexcept {
  int white = 0, black = 0;
  for (const Color color : {Color::White, Color::Black}) {
    const Square king = board.find_king(color);
    const int safety = king.is_valid() && board.is_square_attacked(king, opposite(color)) ? -25 : 0;
    (color == Color::White ? white : black) = safety;
  }
  return white_score(white, black, perspective);
}

int evaluate_attack_pressure(const Board& board, Color perspective) noexcept {
  return white_score(pressure_for(board, Color::White), pressure_for(board, Color::Black), perspective);
}

int evaluate(const Board& board) noexcept {
  const Color side = board.side_to_move();
  return evaluate_material(board, side) + evaluate_piece_activity(board, side) * 2 +
         evaluate_mobility(board, side) + evaluate_king_safety(board, side) +
         evaluate_attack_pressure(board, side);
}

}  // namespace hebichess
