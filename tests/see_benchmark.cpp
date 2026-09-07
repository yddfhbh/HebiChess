#include <chrono>
#include <iostream>

#include "chess/search.hpp"
#include "chess/uci.hpp"

using namespace hebichess;

namespace {

void run(const Board& position, const char* label, bool pruning, bool heuristics) {
  SearchLimits limits;
  limits.max_depth = 3;
  limits.use_tt = false;
  limits.use_see_pruning = pruning;
  limits.use_killer_history = heuristics;
  const auto started = std::chrono::steady_clock::now();
  const SearchResult result = search(position, limits);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started).count();
  std::cout << label << " bestmove " << move_to_uci(result.best_move)
            << " score " << result.score << " nodes " << result.nodes
            << " qnodes " << result.qnodes << " see_calls " << result.see_calls
            << " see_prunes " << result.see_prunes
            << " killer_cutoffs " << result.killer_cutoffs
            << " killer_uses " << result.killer_uses
            << " history_cutoffs " << result.history_cutoffs
            << " elapsed_ms " << elapsed << '\n';
}

void run_timed(const Board& position, const char* label, bool heuristics) {
  SearchLimits limits;
  limits.max_depth = 64;
  limits.has_deadline = true;
  limits.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
  limits.use_tt = false;
  limits.use_killer_history = heuristics;
  int completed_depth = 0;
  const auto started = std::chrono::steady_clock::now();
  const SearchResult result = search(position, limits,
      [&completed_depth](int depth, int, std::uint64_t, std::uint64_t) {
        completed_depth = depth;
      });
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started).count();
  std::cout << label << " depth " << completed_depth
            << " bestmove " << move_to_uci(result.best_move)
            << " score " << result.score << " nodes " << result.nodes
            << " elapsed_ms " << elapsed << '\n';
}

}  // namespace

int main() {
  const Board startpos = Board::initial();
  const Board position = Board::from_fen(
      "r2q1rk1/ppp1bppp/2np4/8/2B1P3/2N1BN2/PPP2PPP/R2Q1RK1 w - - 0 1").value();
  run(startpos, "start_off", true, false);
  clear_transposition_table();
  clear_search_heuristics();
  run(startpos, "start_on", true, true);
  clear_transposition_table();
  clear_search_heuristics();
  run(position, "quiet_off", true, false);
  clear_transposition_table();
  clear_search_heuristics();
  run(position, "quiet_on", true, true);
  clear_transposition_table();
  clear_search_heuristics();
  run_timed(startpos, "timed_off", false);
  clear_transposition_table();
  clear_search_heuristics();
  run_timed(startpos, "timed_on", true);
}
