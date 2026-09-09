#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <tuple>

#include "chess/search.hpp"
#include "chess/uci.hpp"

using namespace hebichess;

namespace {

void run(const Board& position, const char* label, bool pvs, bool aspiration,
         int depth = 4) {
  SearchLimits limits;
  limits.max_depth = depth;
  limits.use_tt = true;
  limits.use_pvs = pvs;
  limits.use_aspiration = aspiration;
  const auto started = std::chrono::steady_clock::now();
  const SearchResult result = search(position, limits);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started).count();
  std::cout << label << " bestmove " << move_to_uci(result.best_move)
            << " score " << result.score << " nodes " << result.nodes
            << " main_nodes " << result.main_nodes << " qnodes " << result.qnodes
            << " qdelta_prunes " << result.qdelta_prunes << " see_calls " << result.see_calls
            << " see_prunes " << result.see_prunes
            << " killer_cutoffs " << result.killer_cutoffs
            << " killer_uses " << result.killer_uses
            << " history_cutoffs " << result.history_cutoffs
            << " null_attempts " << result.null_attempts
            << " null_cutoffs " << result.null_cutoffs
            << " lmr_attempts " << result.lmr_attempts
            << " lmr_researches " << result.lmr_researches
            << " lmr_reduced_nodes " << result.lmr_reduced_search_nodes
            << " lmr_research_nodes " << result.lmr_research_nodes
            << " pvs_zero_window_searches " << result.pvs_zero_window_searches
            << " pvs_researches " << result.pvs_researches
            << " pvs_research_nodes " << result.pvs_research_nodes
            << " root_style_candidates " << result.root_style_candidates
            << " root_style_prefilter_skips " << result.root_style_prefilter_skips
            << " root_style_verifications " << result.root_style_verification_searches
            << " root_style_nodes " << result.root_style_verification_nodes
            << " root_style_verified " << result.root_style_verified
            << " root_style_rejected " << result.root_style_rejected
            << " style_evaluations " << result.style_evaluations
            << " aspiration_retries " << result.aspiration_retries
            << " aspiration_fail_highs " << result.aspiration_fail_highs
            << " aspiration_fail_lows " << result.aspiration_fail_lows
            << " tt_hits " << result.tt_hits
            << " tt_cutoffs " << result.tt_cutoffs
            << " completed_depth " << depth
            << " elapsed_ms " << elapsed
            << " nps " << (elapsed > 0 ? result.nodes * 1000 / elapsed : 0) << '\n';
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

int main(int argc, char*[]) {
  const Board startpos = Board::initial();
  Board qe5 = Board::initial();
  for (const char* move : {"e2e4", "b8c6", "d2d4", "g8h6", "e4e5", "d7d6",
                           "e5d6", "d8d6", "g1f3"}) {
    const auto legal = generate_legal_moves(qe5);
    const auto it = std::find_if(legal.begin(), legal.end(), [move](const Move& candidate) {
      return move_to_uci(candidate) == move;
    });
    if (it == legal.end()) return 1;
    qe5.make_move(*it);
  }
  const std::pair<const char*, const Board*> benchmark_positions[] = {
      {"start", &startpos}, {"qe5", &qe5}};
  for (const auto& [name, position] : benchmark_positions) {
    if (argc > 1 && std::string(name) != "qe5") continue;
    for (const int depth : {6, 7, 8}) {
      clear_transposition_table();
      clear_search_heuristics();
      const std::string label = std::string(name) + "_d" + std::to_string(depth);
      run(*position, label.c_str(), true, true, depth);
    }
  }
}
