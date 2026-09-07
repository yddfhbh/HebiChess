#include <cassert>
#include <string>

#include "chess/perft.hpp"

using namespace hebichess;

namespace {

Board position(const char* fen) {
  const auto board = Board::from_fen(fen);
  assert(board.has_value());
  return *board;
}

void test_start_position() {
  auto board = position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  assert(perft(board, 1) == 20);
  assert(perft(board, 2) == 400);
  assert(perft(board, 3) == 8902);
  assert(perft(board, 4) == 197281);
}

void test_standard_positions() {
  auto kiwipete = position("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  assert(perft(kiwipete, 1) == 48);
  assert(perft(kiwipete, 2) == 2039);
  assert(perft(kiwipete, 3) == 97862);
  assert(perft(kiwipete, 4) == 4085603);

  auto position_three = position("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
  assert(perft(position_three, 1) == 14);
  assert(perft(position_three, 2) == 191);
  assert(perft(position_three, 3) == 2812);
}

void test_make_unmake_special_moves() {
  {
    auto board = Board::initial();
    const std::string original = board.to_fen();
    for (const Move& move : generate_legal_moves(board)) {
      const UndoState undo = make_move(board, move);
      unmake_move(board, move, undo);
      assert(board.to_fen() == original);
    }
  }
  for (const char* fen : {
           "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1",
           "4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1",
           "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
           "4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1",
           "4k3/P7/8/8/8/8/8/4K3 w - - 0 1",
           "1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1"}) {
    auto board = position(fen);
    const std::string original = board.to_fen();
    for (const Move& move : generate_pseudo_legal_moves(board)) {
      const UndoState undo = make_move(board, move);
      unmake_move(board, move, undo);
      assert(board.to_fen() == original);
    }
  }
}

void test_legal_move_rules() {
  auto pinned = position("k3r3/8/8/8/8/8/4R3/4K3 w - - 0 1");
  assert(generate_legal_moves(pinned).size() < generate_pseudo_legal_moves(pinned).size());

  auto mate = position("7k/5Q2/7K/8/8/8/8/8 b - - 0 1");
  assert(generate_legal_moves(mate).empty());
  auto stale = position("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
  assert(generate_legal_moves(stale).empty());

  auto illegal_ep = position("k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
  for (const Move& move : generate_legal_moves(illegal_ep))
    assert(move.flag != MoveFlag::EnPassant);
}

}

int main() {
  test_start_position();
  test_standard_positions();
  test_make_unmake_special_moves();
  test_legal_move_rules();
}
