#include <iostream>
#include <optional>
#include <string>
#include "chess/board.hpp"
#include "chess/eval.hpp"
using namespace hebichess;

int premium(const std::string& fen) {
  auto b = Board::from_fen(fen);
  if (!b) { std::cerr << "INVALID_FEN " << fen << "\n"; return -999; }
  return evaluate_chaos(*b) - evaluate_hce(*b);
}
int main() {
  // In these sparse fixtures there is no tactical-complications bonus.
  const int white7 = premium("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
  const int white6 = premium("4k3/8/P7/8/8/8/8/4K3 w - - 0 1");
  const int white5 = premium("4k3/8/8/P7/8/8/8/4K3 w - - 0 1");
  const int black7 = premium("4k3/8/8/8/8/8/p7/4K3 b - - 0 1");
  const int blocked = premium("r3k3/P7/8/8/8/8/8/4K3 w - - 0 1");
  const int no_passer = premium("4k3/1p6/P7/8/8/8/8/4K3 w - - 0 1");
  // A rook on a8 blocks an a7 pawn, while a black b7 pawn makes
  // a white a6 pawn non-passed. The test does not special-case FENs.
  if (white7 != 75 || white6 != 30 || white5 != 10 ||
      black7 != 75 || blocked >= white7 || no_passer != 0) {
    std::cerr << "PASSER_TEST_FAILED white7=" << white7
              << " white6=" << white6 << " white5=" << white5
              << " black7=" << black7 << " blocked=" << blocked
              << " no_passer=" << no_passer << "\n";
    return 1;
  }
  std::cout << "V492_PASSER_POTENTIAL=PASS" << " white7=" << white7
            << " white6=" << white6 << " white5=" << white5
            << " black7=" << black7 << " blocked=" << blocked << "\n";
  return 0;
}
