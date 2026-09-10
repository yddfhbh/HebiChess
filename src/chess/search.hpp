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

enum class SacrificeKind : std::uint8_t {
  None,
  MinorOrPawnSacrifice,
  ExchangeSacrifice,
  MajorSacrifice,
};

struct RootMoveInfo {
  Move move{};
  int search_score{0};
  ScoreBound bound{ScoreBound::Exact};
  // A threshold proof is sufficient for style selection, but is not an exact
  // minimax score.  Keep that distinction explicit for callers.
  bool style_safe{false};
  int style_score{0};
  bool sacrifice_candidate{false};
  SacrificeKind sacrifice_kind{SacrificeKind::None};
  int style_tolerance{AGGRESSION_TOLERANCE_CP};
  bool king_break{false};
  bool sacrifice_preparation{false};
  // Root-only diagnostic details.  These are deliberately derived from the
  // style layer and never fed back into HCE, NNUE, or negamax evaluation.
  int attack_check_reward{0};
  int attack_ring_reward{0};
  int attack_participant_reward{0};
  int attack_motif_reward{0};
  int attack_line_reward{0};
  int attack_threat_reward{0};
  int attack_escape_reward{0};
  int concrete_attack_bonus{0};
  int shield_pawns_removed{0};
  int opened_king_lines{0};
  int sacrifice_motif_delta{0};
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
  std::uint64_t root_style_verification_proven{0};
  std::uint64_t root_style_verification_nodes{0};
  std::uint64_t root_style_verified{0};
  std::uint64_t root_style_rejected{0};
  std::uint64_t root_style_verification_timeouts{0};
  std::uint64_t style_evaluations{0};
  // Optional root-style profiler.  It is disabled by default so its clocks do
  // not affect normal search benchmarks.
  std::uint64_t root_style_metadata_time_us{0};
  std::uint64_t style_before_attack_time_us{0};
  std::uint64_t style_after_attack_time_us{0};
  std::uint64_t style_king_shield_time_us{0};
  std::uint64_t style_sacrifice_motifs_time_us{0};
  std::uint64_t style_legal_move_generations{0};
  std::uint64_t style_see_calls{0};
  std::uint64_t style_gives_check_calls{0};
  std::uint64_t style_sacrifice_calculations{0};
  std::uint64_t style_child_boards{0};
  std::uint64_t objective_time_ms{0};
  std::uint64_t style_verification_time_ms{0};
  std::uint64_t style_verification_max_ms{0};
  std::uint64_t style_verification_reserve_ms{0};
  std::uint64_t root_style_shortlist{0};
  bool style_verification_reserve_active{false};
  // Records the evaluator requested for threshold proofs.  This keeps the
  // objective/proof evaluator pairing observable in regression tests.
  EvalMode style_verification_eval_mode{EvalMode::HCE};
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
  // Diagnostic switch only: false reproduces the v2.1 root safety margin.
  bool use_style_v3{true};
  // -1 selects the v3.2 adaptive reserve.  A non-negative value is intended
  // for controlled benchmarks of the root verification budget.
  int style_verification_reserve_ms{-1};
  bool profile_style_metadata{false};
  EvalMode eval_mode{EvalMode::HCE};
  // 64 keeps a timed objective search's clock overhead negligible while
  // bounding deadline observation to a small node batch.  Verification
  // proofs remain strict regardless of this setting.
  std::uint64_t deadline_check_interval_nodes{64};
};

using SearchInfoCallback = std::function<void(int, int, std::uint64_t,
                                               std::uint64_t)>;

int evaluate_move_style(const Board& before, const Move& move,
                        const Board& after) noexcept;
bool is_sacrifice_candidate(const Board& before, const Move& move,
                            const Board& after) noexcept;
bool is_style_score_safe(int objective_score, int candidate_score,
                         int tolerance) noexcept;
int negamax(Board& board, int depth, int alpha, int beta, int ply,
            std::uint64_t& nodes);
int quiescence(Board& board, int alpha, int beta, int ply);
SearchResult search(const Board& board, int max_depth);
SearchResult search(const Board& board, const SearchLimits& limits,
                    const SearchInfoCallback& on_iteration = {});
void clear_transposition_table() noexcept;
void clear_search_heuristics() noexcept;

}  // namespace hebichess
