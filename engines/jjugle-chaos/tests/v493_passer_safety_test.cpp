#include <iostream>
#include <optional>
#include <string>
#include "chess/board.hpp"
#include "chess/eval.hpp"
using namespace hebichess;

int premium(const std::string& fen) {
  const auto board = Board::from_fen(fen);
  if (!board) { std::cerr << "BAD_TEST_FEN: " << fen << "\n"; return -9999; }
  return evaluate_chaos(*board) - evaluate_hce(*board);
}

int main() {
  // V492 baseline passers without interception remain exactly unchanged.
  const int white7 = premium("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
  const int white6 = premium("4k3/8/P7/8/8/8/8/4K3 w - - 0 1");
  const int white5 = premium("4k3/8/8/P7/8/8/8/4K3 w - - 0 1");
  const int black7 = premium("4k3/8/8/8/8/8/p7/4K3 b - - 0 1");

  // A distant enemy rook blocks a future promotion square, but not the
  // immediate next square. The V492 30cp premium should be discounted.
  const int distant_blocker_white = premium("r3k3/8/P7/8/8/8/8/4K3 w - - 0 1");
  const int distant_blocker_black = premium("4k3/8/8/8/8/p7/8/R3K3 b - - 0 1");
  const int clear_with_rook_white = premium("1r2k3/8/P7/8/8/8/8/4K3 w - - 0 1");
  const int clear_with_rook_black = premium("4k3/8/8/8/8/p7/8/1R2K3 b - - 0 1");
  // Enemy king on b8 can intercept a5-a8, even if a6 is currently free.
  const int king_intercept_white = premium("1k6/8/8/P7/8/8/8/4K3 w - - 0 1");
  const int king_intercept_black = premium("4k3/8/8/8/p7/8/8/1K6 b - - 0 1");
  const int non_passer = premium("4k3/1p6/P7/8/8/8/8/4K3 w - - 0 1");

  if (white7 != 75 || white6 != 30 || white5 != 10 || black7 != 75 ||
      distant_blocker_white >= clear_with_rook_white ||
      distant_blocker_black >= clear_with_rook_black ||
      king_intercept_white >= white5 || king_intercept_black >= white5 ||
      non_passer != 0) {
    std::cerr << "V493_PASSER_SAFETY_FAILED: white=" << white7 << "," << white6
              << "," << white5 << " black=" << black7
              << " rook_block=" << distant_blocker_white << "," << distant_blocker_black
              << " rook_clear=" << clear_with_rook_white << "," << clear_with_rook_black
              << " king=" << king_intercept_white << "," << king_intercept_black
              << " non_passer=" << non_passer << "\n";
    return 1;
  }
  std::cout << "V493_PASSER_SAFETY=PASS" << "\n";
  return 0;
}
