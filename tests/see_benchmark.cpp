#include <chrono>
#include <iostream>
#include <string>
#include <tuple>

#include "chess/search.hpp"
#include "chess/uci.hpp"

using namespace hebichess;

namespace {

void run(const Board& position, const char* label, bool pvs, bool aspiration) {
  SearchLimits limits;
  limits.max_depth = 4;
  limits.use_tt = true;
  limits.use_pvs = pvs;
  limits.use_aspiration = aspiration;
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
            << " null_attempts " << result.null_attempts
            << " null_cutoffs " << result.null_cutoffs
            << " lmr_attempts " << result.lmr_attempts
            << " lmr_researches " << result.lmr_researches
            << " pvs_zero_window_searches " << result.pvs_zero_window_searches
            << " pvs_researches " << result.pvs_researches
            << " aspiration_retries " << result.aspiration_retries
            << " aspiration_fail_highs " << result.aspiration_fail_highs
            << " aspiration_fail_lows " << result.aspiration_fail_lows
            << " elapsed_ms " << elapsed << '\n';
}

void run_timed(const Board& position, const char* label, bool pvs, bool aspiration) {
  SearchLimits limits;
  limits.max_depth = 64;
  limits.has_deadline = true;
  limits.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
  limits.use_tt = false;
  limits.use_pvs = pvs;
  limits.use_aspiration = aspiration;
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
            << " qnodes " << result.qnodes
            << " pvs_zero_window_searches " << result.pvs_zero_window_searches
            << " pvs_researches " << result.pvs_researches
            << " aspiration_retries " << result.aspiration_retries
            << " aspiration_fail_highs " << result.aspiration_fail_highs
            << " aspiration_fail_lows " << result.aspiration_fail_lows
            << " elapsed_ms " << elapsed << '\n';
}

}  // namespace

int main() {
  const Board startpos = Board::initial();
  const Board quiet = Board::from_fen(
      "r2q1rk1/ppp1bppp/2np4/8/2B1P3/2N1BN2/PPP2PPP/R2Q1RK1 w - - 0 1").value();
  const Board tactical = Board::from_fen(
      "r1bq1rk1/ppp2ppp/2np4/8/2B1P3/2N1BN2/PPP2PPP/R2Q1RK1 w - - 0 1").value();
  const std::pair<const char*, const Board*> positions[] = {
      {"start", &startpos}, {"quiet", &quiet}, {"tactical", &tactical}};
  for (const auto& [name, position] : positions) {
    for (const auto& [pvs, aspiration, suffix] : {
             std::tuple<bool, bool, const char*>{false, false, "A"},
             std::tuple<bool, bool, const char*>{true, false, "B"},
             std::tuple<bool, bool, const char*>{false, true, "C"},
             std::tuple<bool, bool, const char*>{true, true, "D"}}) {
      clear_transposition_table();
      clear_search_heuristics();
      const std::string label = std::string(name) + "_" + suffix;
      run(*position, label.c_str(), pvs, aspiration);
    }
  }
  clear_transposition_table();
  clear_search_heuristics();
  run_timed(startpos, "timed_A", false, false);
  clear_transposition_table();
  clear_search_heuristics();
  run_timed(startpos, "timed_D", true, true);
}
