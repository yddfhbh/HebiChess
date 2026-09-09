#pragma once

#include <cstdint>
#include <functional>
#include <chrono>
#include <vector>

#include "chess/eval.hpp"
#include "chess/movegen.hpp"
#include "chess/tt.hpp"

namespace hebichess {

constexpr int MATE_SCORE = 30000;

enum class ScoreBound : std::uint8_t { Exact, Lower, Upper };

struct RootMoveInfo {
  Move move{};
  int search_score{0};
  ScoreBound bound{ScoreBound::Exact};
  // A threshold proof is sufficient for style selection, but is not an exact
  // minimax score.  Keep that distinction explicit for callers.
  bool style_safe{false};
  int style_score{0};
  bool sacrifice_candidate{false};
  int see_score{0};
};

struct SearchResult {
  Move best_move{};
  int score{0};
  // Updated only after all root moves (and any aspiration retry) finish.
  // A deadline can therefore never expose a partial root iteration as final.
  int completed_depth{0};
  std::uint64_t nodes{0};
  std::uint64_t main_nodes{0};
  std::uint64_t qnodes{0};
  std::uint64_t qdelta_prunes{0};
  std::uint64_t tt_probes{0};
  std::uint64_t tt_hits{0};
  std::uint64_t tt_cutoffs{0};
  std::uint64_t see_calls{0};
  std::uint64_t see_prunes{0};
  std::uint64_t killer_cutoffs{0};
  std::uint64_t killer_uses{0};
  std::uint64_t history_cutoffs{0};
  std::uint64_t null_attempts{0};
  std::uint64_t null_cutoffs{0};
  std::uint64_t lmr_attempts{0};
  std::uint64_t lmr_researches{0};
  std::uint64_t lmr_reduced_search_nodes{0};
  std::uint64_t lmr_research_nodes{0};
  std::uint64_t pvs_zero_window_searches{0};
  std::uint64_t pvs_researches{0};
  std::uint64_t pvs_research_nodes{0};
  std::uint64_t root_style_candidates{0};
  std::uint64_t root_style_prefilter_skips{0};
  std::uint64_t root_style_verification_searches{0};
  std::uint64_t root_style_verification_nodes{0};
  std::uint64_t root_style_verified{0};
  std::uint64_t root_style_rejected{0};
  std::uint64_t style_evaluations{0};
  std::uint64_t aspiration_retries{0};
  std::uint64_t aspiration_fail_highs{0};
  std::uint64_t aspiration_fail_lows{0};
  std::vector<RootMoveInfo> root_moves{};
};

struct SearchLimits {
  int max_depth{1};
  bool has_deadline{false};
  std::chrono::steady_clock::time_point deadline{};
  bool use_tt{true};
  bool use_see_pruning{true};
  bool use_killer_history{true};
  bool use_null_move{true};
  bool use_lmr{true};
  bool use_pvs{true};
  bool use_aspiration{true};
  EvalMode eval_mode{EvalMode::HCE};
};

using SearchInfoCallback = std::function<void(int, int, std::uint64_t,
                                               std::uint64_t)>;

int evaluate_move_style(const Board& before, const Move& move,
                        const Board& after) noexcept;
bool is_sacrifice_candidate(const Board& before, const Move& move,
                            const Board& after) noexcept;
int negamax(Board& board, int depth, int alpha, int beta, int ply,
            std::uint64_t& nodes);
int quiescence(Board& board, int alpha, int beta, int ply);
SearchResult search(const Board& board, int max_depth);
SearchResult search(const Board& board, const SearchLimits& limits,
                    const SearchInfoCallback& on_iteration = {});
void clear_transposition_table() noexcept;
void clear_search_heuristics() noexcept;

}  // namespace hebichess
