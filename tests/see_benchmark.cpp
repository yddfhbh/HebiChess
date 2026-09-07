#include <chrono>
#include <iostream>

#include "chess/search.hpp"
#include "chess/uci.hpp"

using namespace hebichess;

namespace {

void run(const Board& position, const char* label, bool pruning) {
  SearchLimits limits;
  limits.max_depth = 3;
  limits.use_tt = false;
  limits.use_see_pruning = pruning;
  const auto started = std::chrono::steady_clock::now();
  const SearchResult result = search(position, limits);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started).count();
  std::cout << label << " bestmove " << move_to_uci(result.best_move)
            << " score " << result.score << " nodes " << result.nodes
            << " qnodes " << result.qnodes << " see_calls " << result.see_calls
            << " see_prunes " << result.see_prunes << " elapsed_ms " << elapsed << '\n';
}

}  // namespace

int main() {
  const Board position = Board::from_fen(
      "r2q1rk1/ppp1bppp/2np4/8/2B1P3/2N1BN2/PPP2PPP/R2Q1RK1 w - - 0 1").value();
  run(position, "see_off", false);
  clear_transposition_table();
  run(position, "see_on", true);
}
