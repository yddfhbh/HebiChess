#include <cassert>
#include <iostream>

#include "chess/see.hpp"
#include "chess/uci.hpp"

using namespace hebichess;

namespace {

Board board(const char* fen) { return Board::from_fen(fen).value(); }

int see(const char* fen, const char* uci) {
  Board position = board(fen);
  const auto move = parse_uci_move(position, uci);
  assert(move.has_value());
  return static_exchange_eval(position, *move);
}

void test_basic_exchanges() {
  assert(see("4k3/8/8/3q4/4P3/8/8/4K3 w - - 0 1", "e4d5") == 900);
  assert(see("3rk3/8/8/8/8/8/3p4/3QK3 w - - 0 1", "d1d2") == -300);
  assert(see("4k3/8/2p5/3p4/4P3/8/8/4K3 w - - 0 1", "e4d5") == 0);
  assert(see("4k3/8/8/8/1p6/n7/8/R3K3 w - - 0 1", "a1a3") == -180);
  assert(see("4k3/8/2p5/3q4/4P3/8/8/4K3 w - - 0 1", "e4d5") == 800);
  assert(see("3qk3/8/8/3p4/2B5/8/8/4K3 w - - 0 1", "c4d5") == -230);
}

void test_special_moves_and_directions() {
  assert(see("1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7b8q") == 1300);
  assert(see("4k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7a8q") == 800);
  assert(see("3rk3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6") == 0);
  assert(see("4k3/8/8/8/4p3/3Q4/8/4K3 b - - 0 1", "e4d3") == 900);
}

void test_king_capture_legality() {
  // The black king cannot recapture onto a square protected by the white rook.
  assert(see("8/8/8/4k3/4p3/3Q4/8/K3R3 w - - 0 1", "d3e4") == 100);
}

}  // namespace

int main() {
  test_basic_exchanges();
  test_special_moves_and_directions();
  test_king_capture_legality();
  std::cout << "SEE tests passed\n";
}
