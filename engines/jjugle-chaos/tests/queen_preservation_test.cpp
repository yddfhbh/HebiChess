#include <iostream>
#include <array>
#include <string>
#include <algorithm>
#include "chess/search.hpp"
#include "chess/movegen.hpp"
#include "chess/uci.hpp"
using namespace hebichess;
int main() {
  // FENs extracted from the user's eight-game V4.7 vs V4.5 pilot, immediately
  // before both queens were exchanged. Do not overfit one forced exchange.
  const std::array<const char*, 7> positions{
    "r1b1kb1r/3pnppp/2p5/p3PqQ1/Pp1p1B2/1B1P4/1PP2PPP/RN3RK1 b kq - 1 13",
    "r1bkr3/pppp1Bpp/5n2/8/1bQq4/8/PP1P1P1P/RNBK1R2 b - - 3 14",
    "r4r2/ppp2kp1/2bbnq1p/5Q2/2B2n2/2P2P2/PP4PP/RNB2RK1 w - - 7 24",
    "r4b1r/ppNkpppp/3p4/2p1n1N1/4P1q1/7Q/PPnP1PPP/R1B2K1R b - - 1 13",
    "r3kb1r/p2b1ppp/3ppn2/1p2q3/2p5/2N2N2/P1PPQPPP/R1B2RK1 b kq - 0 15",
    "rn2k1nr/pQ1b1ppp/1q2p3/8/1b6/8/PP1NPPPP/R1B1KBNR w KQkq - 1 9",
    "r3k2r/pppb1ppp/2n2n2/3pN3/1q1P4/1Q6/PP1NPPPP/R3KB1R w KQkq - 6 11"
  };
  int changed = 0;
  for (const char* fen : positions) {
    const auto board = Board::from_fen(fen);
    if (!board) { std::cerr << "INVALID_FEN\n"; return 1; }
    clear_transposition_table();
    clear_search_heuristics();
    SearchLimits limits;
    limits.max_depth = 7;
    limits.eval_mode = EvalMode::CHAOS;
    const SearchResult result = search(*board, limits);
    const auto moves = generate_legal_moves(*board);
    if (std::find(moves.begin(), moves.end(), result.best_move) == moves.end()) {
      std::cerr << "ILLEGAL_BESTMOVE\n"; return 1;
    }
    if (result.v48_queen_preserved) {
      ++changed;
      if (result.v48_queen_preservation_loss_cp > 110 ||
          result.v48_queen_preservation_loss_cp < -1) {
        std::cerr << "UNSAFE_QUEEN_PRESERVATION\n";return 1;
      }
      std::cout << "PRESERVED " << move_to_uci(result.best_move)
                << " exact_loss_or_minus1 "
                << result.v48_queen_preservation_loss_cp << '\n';
    }
  }
  if (changed < 1) { std::cerr << "NO_QUEEN_EXCHANGES_AVOIDED\n"; return 1; }
  auto opening = Board::from_fen(positions[2]);
  clear_transposition_table();
  clear_search_heuristics();
  SearchLimits hce;
  hce.max_depth = 7;
  hce.eval_mode = EvalMode::HCE;
  if (search(*opening, hce).v48_queen_preserved) {
    std::cerr << "HCE_INADVERTENTLY_MODIFIED\n";return 1;
  }
  std::cout << "V48_QUEEN_PRESERVATION_TEST=PASS changed=" << changed << '\n';
  return 0;
}
