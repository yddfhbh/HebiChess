#include <algorithm>
#include <cassert>

#include "chess/movegen.hpp"

using namespace hebichess;

namespace {

Square sq(char file, int rank) { return Square::from_file_rank(file - 'a', rank - 1); }

bool has_move(const std::vector<Move>& moves, Square from, Square to,
              MoveFlag flag = MoveFlag::Normal, PieceType promotion = PieceType::None) {
  return std::any_of(moves.begin(), moves.end(), [&](const Move& move) {
    return move.from == from && move.to == to && move.flag == flag &&
           move.promotion == promotion;
  });
}

void test_initial_and_piece_moves() {
  assert(generate_pseudo_legal_moves(Board::initial()).size() == 20);

  auto knight = Board::from_fen("4k3/8/8/3N4/8/8/8/4K3 w - - 0 1").value();
  auto knight_moves = generate_pseudo_legal_moves(knight);
  for (const auto [file, rank] : {std::pair{'e', 7}, std::pair{'f', 6}, std::pair{'f', 4},
                                  std::pair{'e', 3}, std::pair{'c', 3}, std::pair{'b', 4},
                                  std::pair{'b', 6}, std::pair{'c', 7}})
    assert(has_move(knight_moves, sq('d', 5), sq(file, rank)));
  knight = Board::from_fen("N3k3/8/8/8/8/8/8/4K3 w - - 0 1").value();
  knight_moves = generate_pseudo_legal_moves(knight);
  assert(has_move(knight_moves, sq('a', 8), sq('b', 6)));
  assert(has_move(knight_moves, sq('a', 8), sq('c', 7)));

  const auto bishop = Board::from_fen("4k3/8/8/3B4/2P5/8/8/4K3 w - - 0 1").value();
  assert(!has_move(generate_pseudo_legal_moves(bishop), sq('d', 5), sq('b', 3)));
  const auto rook = Board::from_fen("4k3/8/8/3R4/3P4/8/8/4K3 w - - 0 1").value();
  assert(!has_move(generate_pseudo_legal_moves(rook), sq('d', 5), sq('d', 3)));
  const auto queen = Board::from_fen("4k3/8/8/3Q4/8/8/8/4K3 w - - 0 1").value();
  assert(has_move(generate_pseudo_legal_moves(queen), sq('d', 5), sq('h', 5)));
  const auto king = Board::from_fen("4k3/8/8/8/3K4/8/8/8 w - - 0 1").value();
  assert(generate_pseudo_legal_moves(king).size() == 8);
}

void test_pawns_and_special_moves() {
  auto board = Board::from_fen("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1").value();
  auto moves = generate_pseudo_legal_moves(board);
  assert(has_move(moves, sq('e', 2), sq('e', 3)));
  assert(has_move(moves, sq('e', 2), sq('e', 4), MoveFlag::DoublePawnPush));

  board = Board::from_fen("4k3/4p3/8/8/8/8/8/4K3 b - - 0 1").value();
  moves = generate_pseudo_legal_moves(board);
  assert(has_move(moves, sq('e', 7), sq('e', 6)));
  assert(has_move(moves, sq('e', 7), sq('e', 5), MoveFlag::DoublePawnPush));

  board = Board::from_fen("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1").value();
  assert(has_move(generate_pseudo_legal_moves(board), sq('e', 4), sq('d', 5), MoveFlag::Capture));
  board = Board::from_fen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1").value();
  assert(has_move(generate_pseudo_legal_moves(board), sq('e', 5), sq('d', 6), MoveFlag::EnPassant));

  board = Board::from_fen("4k3/P7/8/8/8/8/8/4K3 w - - 0 1").value();
  moves = generate_pseudo_legal_moves(board);
  for (const PieceType promotion : {PieceType::Queen, PieceType::Rook,
                                    PieceType::Bishop, PieceType::Knight})
    assert(has_move(moves, sq('a', 7), sq('a', 8), MoveFlag::Promotion, promotion));
  board = Board::from_fen("1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1").value();
  moves = generate_pseudo_legal_moves(board);
  assert(has_move(moves, sq('a', 7), sq('b', 8), MoveFlag::PromotionCapture, PieceType::Queen));
  assert(std::count_if(moves.begin(), moves.end(), [](const Move& move) {
           return move.flag == MoveFlag::PromotionCapture;
         }) == 4);
}

void test_castling_and_attacks() {
  auto board = Board::from_fen("4k3/8/8/8/8/8/8/4K2R w K - 0 1").value();
  assert(has_move(generate_pseudo_legal_moves(board), sq('e', 1), sq('g', 1), MoveFlag::CastleKingSide));
  board = Board::from_fen("4k3/8/8/8/8/8/8/R3K3 w Q - 0 1").value();
  assert(has_move(generate_pseudo_legal_moves(board), sq('e', 1), sq('c', 1), MoveFlag::CastleQueenSide));
  board = Board::from_fen("4k3/8/8/8/8/8/8/R2BK2R w KQ - 0 1").value();
  const auto blocked_moves = generate_pseudo_legal_moves(board);
  assert(blocked_moves.end() == std::find_if(
      blocked_moves.begin(), blocked_moves.end(),
      [](const Move& move) { return move.flag == MoveFlag::CastleQueenSide; }));
  board = Board::from_fen("4kr2/8/8/8/8/8/8/4K2R w K - 0 1").value();
  assert(!has_move(generate_pseudo_legal_moves(board), sq('e', 1), sq('g', 1), MoveFlag::CastleKingSide));

  board = Board::from_fen("4k3/8/8/8/8/8/3p4/4K3 w - - 0 1").value();
  assert(board.is_square_attacked(sq('c', 1), Color::Black));
  board = Board::from_fen("4k3/8/8/8/8/3n4/8/4K3 w - - 0 1").value();
  assert(board.is_square_attacked(sq('e', 1), Color::Black));
  board = Board::from_fen("4k3/8/8/8/8/8/8/r3K3 w - - 0 1").value();
  assert(board.is_square_attacked(sq('e', 1), Color::Black));
  board = Board::from_fen("4k3/8/8/8/8/8/8/3qK3 w - - 0 1").value();
  assert(board.is_square_attacked(sq('e', 1), Color::Black));
  board = Board::from_fen("8/8/8/8/8/8/3k4/4K3 w - - 0 1").value();
  assert(board.is_square_attacked(sq('e', 1), Color::Black));
}

}  // namespace

int main() {
  test_initial_and_piece_moves();
  test_pawns_and_special_moves();
  test_castling_and_attacks();
}
