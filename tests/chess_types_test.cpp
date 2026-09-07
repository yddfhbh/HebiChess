#include <cassert>

#include "chess/board.hpp"
#include "chess/move.hpp"

namespace {

using hebichess::Board;
using hebichess::Color;
using hebichess::Piece;
using hebichess::PieceType;
using hebichess::Square;

void test_square_conversion() {
  const Square a1 = Square::from_file_rank(0, 0);
  const Square h8 = Square::from_file_rank(7, 7);

  assert(a1.is_valid());
  assert(a1.index() == 0);
  assert(a1.file() == 0);
  assert(a1.rank() == 0);
  assert(h8.index() == 63);
  assert(h8.file() == 7);
  assert(h8.rank() == 7);
  assert(!Square::from_index(64).is_valid());
  assert(!Square::from_file_rank(8, 0).is_valid());
}

void test_piece_state() {
  constexpr Piece empty{};
  constexpr Piece white_king{PieceType::King, Color::White};

  assert(empty.is_empty());
  assert(!empty.is_colored());
  assert(!white_king.is_empty());
  assert(white_king.is_colored());
  assert(hebichess::opposite(Color::White) == Color::Black);
  assert(hebichess::opposite(Color::Black) == Color::White);
}

void test_initial_board() {
  const Board board;
  constexpr Piece white_rook{PieceType::Rook, Color::White};
  constexpr Piece white_queen{PieceType::Queen, Color::White};
  constexpr Piece black_king{PieceType::King, Color::Black};
  constexpr Piece white_pawn{PieceType::Pawn, Color::White};
  constexpr Piece black_pawn{PieceType::Pawn, Color::Black};

  assert(board.piece_at(Square::from_file_rank(0, 0)) == white_rook);
  assert(board.piece_at(Square::from_file_rank(3, 0)) == white_queen);
  assert(board.piece_at(Square::from_file_rank(4, 7)) == black_king);
  assert(board.piece_at(Square::from_file_rank(4, 1)) == white_pawn);
  assert(board.piece_at(Square::from_file_rank(4, 6)) == black_pawn);
  assert(board.piece_at(Square::from_file_rank(4, 3)).is_empty());
}

void test_board_mutation() {
  Board board;
  const Square e4 = Square::from_file_rank(4, 3);
  constexpr Piece white_knight{PieceType::Knight, Color::White};
  board.set_piece(e4, white_knight);
  assert(board.piece_at(e4) == white_knight);
}

void test_move_representation() {
  const Square e2 = Square::from_file_rank(4, 1);
  const Square e4 = Square::from_file_rank(4, 3);
  const Square e7 = Square::from_file_rank(4, 6);
  const Square e8 = Square::from_file_rank(4, 7);
  const hebichess::Move move{e2, e4, PieceType::None,
                             hebichess::MoveFlag::DoublePawnPush};
  const hebichess::Move promotion_move{
      e7, e8, PieceType::Queen, hebichess::MoveFlag::Promotion};

  assert(move.from == e2);
  assert(move.to == e4);
  assert(move.flag == hebichess::MoveFlag::DoublePawnPush);
  assert(!move.is_promotion());
  assert(promotion_move.is_promotion());
}

void test_initial_fen() {
  const auto board = Board::from_fen(
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  constexpr hebichess::CastlingRights all_rights{true, true, true, true};
  assert(board.has_value());
  assert(board->to_fen() ==
         "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  assert(board->side_to_move() == Color::White);
  assert(board->castling_rights() == all_rights);
  assert(!board->en_passant_target().is_valid());
  assert(board->halfmove_clock() == 0);
  assert(board->fullmove_number() == 1);
}

void test_custom_fen() {
  const auto board = Board::from_fen(
      "4k3/8/8/3pP3/8/8/8/4K3 b q e6 17 42");
  constexpr hebichess::CastlingRights black_queen_side{false, false, false,
                                                        true};
  constexpr Piece white_pawn{PieceType::Pawn, Color::White};
  assert(board.has_value());
  assert(board->side_to_move() == Color::Black);
  assert(board->castling_rights() == black_queen_side);
  assert(board->en_passant_target() == Square::from_file_rank(4, 5));
  assert(board->halfmove_clock() == 17);
  assert(board->fullmove_number() == 42);
  assert(board->piece_at(Square::from_file_rank(4, 4)) == white_pawn);
  assert(board->to_fen() ==
         "4k3/8/8/3pP3/8/8/8/4K3 b q e6 17 42");
}

void test_invalid_fen() {
  assert(!Board::from_fen("8/8/8/8/8/8/8/8 w - - 0").has_value());
  assert(!Board::from_fen(
              "8/8/8/8/8/8/8/8 x - - 0 1").has_value());
  assert(!Board::from_fen(
              "8/8/8/8/8/8/8/8 w KK - 0 1").has_value());
  assert(!Board::from_fen(
              "8/8/8/8/8/8/8/8 w - e4 0 1").has_value());
  assert(!Board::from_fen(
              "8/8/8/8/8/8/8/9 w - - 0 1").has_value());
  assert(!Board::from_fen(
              "8/8/8/8/8/8/8/8 w - - 0 0").has_value());
  assert(!Board::from_fen(
              "8/8/8/8/8/8/8/8 w - - 0 1 extra").has_value());
}

}  // namespace

int main() {
  test_square_conversion();
  test_piece_state();
  test_initial_board();
  test_board_mutation();
  test_move_representation();
  test_initial_fen();
  test_custom_fen();
  test_invalid_fen();
}
