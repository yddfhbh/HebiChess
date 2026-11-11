#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include "chess/board.hpp"
#include "chess/uci.hpp"
#include "chess/search.hpp"
#include "chess/movegen.hpp"
using namespace hebichess;

int main() {
  // Lichess AimPVRp7, played by JJUGLE v4.8. This reproduces the
  // board+repetition history, but NONE of the implementation branches
  // on these FENs, players, or exact moves.
  std::vector<std::string> uci = {
    "e2e4", "b8c6", "b2b4", "c6b4", "c1b2", "g8f6", "e4e5", "f6e4",
    "d2d3", "e4g5", "d1d2", "a7a5", "a2a3", "e7e6", "a3b4", "f8b4",
    "c2c3", "b4c5", "h2h4", "c5e7", "d2f4", "d7d6", "e5d6", "d8d6",
    "f4e3", "d6d5", "b2a3", "e7a3", "b1a3", "d5b3", "e3c5", "b3d5",
    "c5c7", "d5b3", "h4g5", "b3b2", "a3c2", "b2c2", "a1a5", "c2c1",
    "e1e2", "c1c2", "e2f3", "a8a5", "c7c8", "e8e7", "c8b7", "e7d8",
    "b7b6", "d8e8", "b6c6", "e8d8", "c6b6", "d8e8",
    "b6c6", "e8d8"
  };
  Board b = Board::initial();
  std::vector<ZobristKey> hist = {b.zobrist_key()};
  for (std::size_t i = 0; i < uci.size(); ++i) {
    auto m = parse_uci_move(b, uci[i]);
    if (!m) { std::cerr << "INVALID_PGN_PLY " << i+1 << " " << uci[i] << " fen=" << b.to_fen() << '\n'; return 1; }
    b.make_move(*m); hist.push_back(b.zobrist_key());
    if (i == 53 || i == 55) {
      std::cout << "after " << i+1 << " plies " << b.to_fen() << '\n';
      SearchLimits limits;
      limits.eval_mode = EvalMode::CHAOS;
      limits.max_depth=4;
      limits.position_history_keys=hist;
      clear_transposition_table();clear_search_heuristics();
      auto result = search(b, limits);
      const std::string chosen = move_to_uci(result.best_move);
      std::cout << "ply=" << i+1 << " best=" << chosen << " score=" << result.score << "\n";
      if (i == 53 && chosen != "b6a5") {
        std::cerr << "WINNING_ROOK_NOT_CAPTURED_ON_MOVE_28\n";
        return 4;
      }
      if (i == 55) {
        if (chosen == "c6b6") {
          std::cerr << "CHOSEN_CLAIMABLE_THREEFOLD_IN_WINNING_POSITION\n";
          return 5;
        }
        const auto repeated = parse_uci_move(b, "c6b6");
        if (!repeated) return 6;
        const auto it = std::find_if(result.root_moves.begin(), result.root_moves.end(),
            [&](const RootMoveInfo& info) { return info.move == *repeated; });
        if (it == result.root_moves.end() || it->search_score != 0) {
          std::cerr << "REPEATED_LINE_NOT_SEARCHED_AS_DRAW\n";
          return 7;
        }
      }
    }
  }
  // At move 29, Qb6 creates an immediate claimable threefold.
  auto q = parse_uci_move(b,"c6b6");
  if (!q) return 2;
  b.make_move(*q);
  const auto repeats = std::count(hist.begin(),hist.end(),b.zobrist_key());
  std::cout << "third occurrence: prior=" << repeats << '\n';
  if (repeats != 2) return 3;
  std::cout << "V494_CONVERSION_REAL_GAME=PASS\n";
  return 0;
}
