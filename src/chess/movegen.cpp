#include "chess/movegen.hpp"

#include <array>

namespace hebichess {
namespace {

Square at(int file, int rank) noexcept {
  if (file < 0 || file >= 8 || rank < 0 || rank >= 8) return {};
  return Square::from_file_rank(static_cast<std::uint8_t>(file),
                                static_cast<std::uint8_t>(rank));
}

bool enemy_at(const Board& board, Square square, Color color) noexcept {
  return square.is_valid() && !board.piece_at(square).is_empty() &&
         board.piece_at(square).color != color;
}

void add_promotion_moves(std::vector<Move>& moves, Square from, Square to,
                         bool capture) {
  constexpr std::array<PieceType, 4> promotions = {
      PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight};
  for (const PieceType promotion : promotions) {
    moves.push_back({from, to, promotion,
                     capture ? MoveFlag::PromotionCapture : MoveFlag::Promotion});
  }
}

void add_pawn_moves(const Board& board, Square from, Color color,
                    std::vector<Move>& moves) {
  const int direction = color == Color::White ? 1 : -1;
  const int start_rank = color == Color::White ? 1 : 6;
  const int promotion_rank = color == Color::White ? 7 : 0;
  const Square one = at(from.file(), from.rank() + direction);
  if (one.is_valid() && board.piece_at(one).is_empty()) {
    if (one.rank() == promotion_rank) {
      add_promotion_moves(moves, from, one, false);
    } else {
      moves.push_back({from, one, PieceType::None, MoveFlag::Normal});
      const Square two = at(from.file(), from.rank() + 2 * direction);
      if (from.rank() == start_rank && two.is_valid() &&
          board.piece_at(two).is_empty()) {
        moves.push_back({from, two, PieceType::None,
                         MoveFlag::DoublePawnPush});
      }
    }
  }

  for (const int file_delta : {-1, 1}) {
    const Square target = at(from.file() + file_delta, from.rank() + direction);
    if (!target.is_valid()) continue;
    const bool capture = enemy_at(board, target, color);
    bool en_passant = false;
    if (!capture && target == board.en_passant_target()) {
      const Square captured = at(target.file(), from.rank());
      en_passant = captured.is_valid() &&
                   board.piece_at(captured).type == PieceType::Pawn &&
                   board.piece_at(captured).color == opposite(color);
    }
    if (capture || en_passant) {
      if (target.rank() == promotion_rank) {
        add_promotion_moves(moves, from, target, capture);
      } else {
        moves.push_back({from, target, PieceType::None,
                         en_passant ? MoveFlag::EnPassant : MoveFlag::Capture});
      }
    }
  }
}

template <std::size_t Count>
void add_sliding_moves(const Board& board, Square from, Color color,
                       const std::array<std::pair<int, int>, Count>& directions,
                       std::vector<Move>& moves) {
  for (const auto [df, dr] : directions) {
    for (int file = from.file() + df, rank = from.rank() + dr;; file += df, rank += dr) {
      const Square target = at(file, rank);
      if (!target.is_valid()) break;
      const Piece piece = board.piece_at(target);
      if (piece.is_empty()) {
        moves.push_back({from, target});
      } else {
        if (piece.color != color) moves.push_back({from, target, PieceType::None,
                                                    MoveFlag::Capture});
        break;
      }
    }
  }
}

void add_castling(const Board& board, Square from, Color color,
                  std::vector<Move>& moves) {
  const CastlingRights rights = board.castling_rights();
  const int rank = color == Color::White ? 0 : 7;
  const bool king_side = color == Color::White ? rights.white_king_side
                                               : rights.black_king_side;
  const bool queen_side = color == Color::White ? rights.white_queen_side
                                                 : rights.black_queen_side;
  const Square king_home = at(4, rank);
  if (from != king_home || board.piece_at(king_home) != Piece{PieceType::King, color}) return;

  auto clear = [&board](int first, int last, int r) {
    for (int file = first; file <= last; ++file)
      if (!board.piece_at(at(file, r)).is_empty()) return false;
    return true;
  };
  if (king_side && board.piece_at(at(7, rank)) == Piece{PieceType::Rook, color} &&
      clear(5, 6, rank) && !board.is_square_attacked(at(4, rank), opposite(color)) &&
      !board.is_square_attacked(at(5, rank), opposite(color)) &&
      !board.is_square_attacked(at(6, rank), opposite(color))) {
    moves.push_back({from, at(6, rank), PieceType::None, MoveFlag::CastleKingSide});
  }
  if (queen_side && board.piece_at(at(0, rank)) == Piece{PieceType::Rook, color} &&
      clear(1, 3, rank) && !board.is_square_attacked(at(4, rank), opposite(color)) &&
      !board.is_square_attacked(at(3, rank), opposite(color)) &&
      !board.is_square_attacked(at(2, rank), opposite(color))) {
    moves.push_back({from, at(2, rank), PieceType::None, MoveFlag::CastleQueenSide});
  }
}

}  // namespace

std::vector<Move> generate_pseudo_legal_moves(const Board& board) {
  std::vector<Move> moves;
  const Color color = board.side_to_move();
  constexpr std::array<std::pair<int, int>, 8> knight_steps = {
      {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}}};
  constexpr std::array<std::pair<int, int>, 8> king_steps = {
      {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}}};
  constexpr std::array<std::pair<int, int>, 4> diagonals = {{{1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};
  constexpr std::array<std::pair<int, int>, 4> orthogonals = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

  for (std::uint8_t index = 0; index < Square::kSquareCount; ++index) {
    const Square from = Square::from_index(index);
    const Piece piece = board.piece_at(from);
    if (piece.is_empty() || piece.color != color) continue;
    switch (piece.type) {
      case PieceType::Pawn: add_pawn_moves(board, from, color, moves); break;
      case PieceType::Knight:
        for (const auto [df, dr] : knight_steps) {
          const Square to = at(from.file() + df, from.rank() + dr);
          if (to.is_valid() && (board.piece_at(to).is_empty() || enemy_at(board, to, color)))
            moves.push_back({from, to, PieceType::None,
                             board.piece_at(to).is_empty() ? MoveFlag::Normal : MoveFlag::Capture});
        }
        break;
      case PieceType::Bishop: add_sliding_moves(board, from, color, diagonals, moves); break;
      case PieceType::Rook: add_sliding_moves(board, from, color, orthogonals, moves); break;
      case PieceType::Queen: {
        add_sliding_moves(board, from, color, diagonals, moves);
        add_sliding_moves(board, from, color, orthogonals, moves);
        break;
      }
      case PieceType::King:
        for (const auto [df, dr] : king_steps) {
          const Square to = at(from.file() + df, from.rank() + dr);
          if (to.is_valid() && (board.piece_at(to).is_empty() || enemy_at(board, to, color)))
            moves.push_back({from, to, PieceType::None,
                             board.piece_at(to).is_empty() ? MoveFlag::Normal : MoveFlag::Capture});
        }
        add_castling(board, from, color, moves);
        break;
      case PieceType::None: break;
    }
  }
  return moves;
}

std::vector<Move> generate_legal_moves(Board& board) {
  std::vector<Move> legal_moves;
  const Color moving_color = board.side_to_move();
  for (const Move& move : generate_pseudo_legal_moves(board)) {
    const UndoState undo = board.make_move(move);
    const Square king = board.find_king(moving_color);
    if (king.is_valid() && !board.is_square_attacked(king, opposite(moving_color))) {
      legal_moves.push_back(move);
    }
    board.unmake_move(move, undo);
  }
  return legal_moves;
}

}  // namespace hebichess
