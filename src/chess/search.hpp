#pragma once

#include <cstdint>
#include <functional>
#include <chrono>
#include <optional>
#include <string>
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
  // QSearch-TT variants are compile-time test binaries only. Production
  // compiles variant 0, which neither creates nor probes a QSearch table.
  std::uint64_t eval_calls{0};
  std::uint64_t tt_probes{0};
  std::uint64_t tt_hits{0};
  std::uint64_t tt_cutoffs{0};
  std::uint64_t tt_stores{0};
  std::uint64_t tt_replacements{0};
  std::uint64_t qtt_probes{0};
  std::uint64_t qtt_hits{0};
  std::uint64_t qtt_exact_hits{0};
  std::uint64_t qtt_lower_hits{0};
  std::uint64_t qtt_upper_hits{0};
  std::uint64_t qtt_same_position_repeats{0};
  std::uint64_t qtt_in_check_probes{0};
  std::uint64_t qtt_in_check_hits{0};
  std::uint64_t qtt_non_check_probes{0};
  std::uint64_t qtt_non_check_hits{0};
  std::uint64_t qtt_potential_reusable_eval{0};
  std::uint64_t qtt_potential_reusable_subtree{0};
  std::uint64_t qtt_active_probes{0};
  std::uint64_t qtt_active_hits{0};
  std::uint64_t qtt_active_cutoffs{0};
  std::uint64_t qtt_stores{0};
  std::uint64_t qtt_replacements{0};
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
  // Candidate means that the stored QTT bound is reusable for the current
  // window and selected diagnostic bound mask.  It includes suppressed
  // candidates when a cutoff limit is in force.
  std::uint64_t qtt_cutoff_candidates{0};
  std::uint64_t qtt_cutoff_applied{0};
#endif
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
  std::uint64_t style_metadata_time_ms{0};
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
  // Adaptive time-management telemetry, emitted by the normal UCI frontend.
  int time_soft_ms{0};
  int time_hard_ms{0};
  int time_target_ms{0};
  int time_elapsed_ms{0};
  bool early_budget{false};
  int attempted_depth{0};
  int latest_iteration_ms{0};
  int time_stability{0};
  int time_score_swing{0};
  bool time_margin_known{false};
  int time_margin{0};
  std::string time_confidence{"low"};
  std::string time_stop_reason{"depth"};
  std::vector<RootMoveInfo> root_moves{};
};

struct SearchLimits {
  int max_depth{1};
  bool has_deadline{false};
  std::chrono::steady_clock::time_point deadline{};
  bool has_soft_deadline{false};
  std::chrono::steady_clock::time_point soft_deadline{};
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
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
  // Test-binary-only QTT controls.  They are intentionally unavailable from
  // UCI and absent from every production compilation unit.
  bool qtt_diagnostic_override_bounds{false};
  std::uint8_t qtt_diagnostic_bound_mask{0};
  std::int64_t qtt_cutoff_limit{-1};
  std::uint64_t qtt_trace_cutoff{0};
  // QSearch-window isolation only.  This never reaches a production target
  // and is intentionally limited to one pruning rule at a time.
  bool qsearch_diagnostic_disable_delta_pruning{false};
#endif
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

#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
// QTT diagnostics are compiled only into the profile/diagnostic binaries.
// They deliberately have no production switch or state.
struct QsearchTtCutoffTrace {
  std::uint64_t cutoff_serial{0};
  ZobristKey key{0};
  ZobristKey stored_key{0};
  std::string hit_fen{};
  std::string store_fen{};
  int ply{0};
  int alpha{0};
  int beta{0};
  TTBound bound{TTBound::Exact};
  int stored_score{0};
  int decoded_score{0};
  std::uint64_t store_serial{0};
  int store_ply{0};
  int store_alpha{0};
  int store_beta{0};
  int store_result{0};
  bool same_fen{false};
  bool same_full_key{false};
  std::optional<float> hit_raw_nnue{};
  std::optional<float> store_raw_nnue{};
  std::uint64_t hit_accumulator_checksum{0};
  std::uint64_t store_accumulator_checksum{0};
  int qtt_off_hit_window{0};
  int qtt_off_store_window{0};
  int qtt_off_full_window{0};
};

// QTT-off, dual-window tracing payload.  It is deliberately available only
// in diagnostic profile binaries so it cannot become a search dependency.
enum class QsearchReturnKind : std::uint8_t {
  Mate,
  StandPatBeta,
  Stalemate,
  EvasionLoop,
  TacticalLoop,
  Stopped,
};

struct QsearchMoveTrace {
  std::string move{};
  bool capture{false};
  bool promotion{false};
  bool gives_check{false};
  int see{0};
  int order{0};
  bool see_rejected{false};
  bool delta_rejected{false};
  bool searched{false};
  int child_alpha{0};
  int child_beta{0};
  int child_score{0};
  int alpha_after{0};
  bool beta_cutoff{false};
};

struct QsearchNodeTrace {
  std::uint64_t sequence{0};
  ZobristKey key{0};
  std::string fen{};
  int ply{0};
  int entry_alpha{0};
  int entry_beta{0};
  bool in_check{false};
  std::optional<float> raw_nnue{};
  std::uint64_t accumulator_checksum{0};
  std::optional<int> stand_pat{};
  int alpha_after_stand_pat{0};
  std::vector<QsearchMoveTrace> moves{};
  QsearchReturnKind return_kind{QsearchReturnKind::Stopped};
  int returned_score{0};
};

struct QsearchWindowTrace {
  int score{0};
  std::vector<QsearchNodeTrace> nodes{};
};

// Starts from an identical board/NNUE accumulator with QTT fully disabled.
// `disable_delta_pruning` is a test-only single-rule experiment.
QsearchWindowTrace trace_qsearch_window_for_test(const Board& board, int alpha, int beta,
                                                  int ply, EvalMode eval_mode,
                                                  bool disable_delta_pruning = false);

using QsearchTtCutoffTraceCallback = std::function<void(const QsearchTtCutoffTrace&)>;

void set_qsearch_tt_cutoff_trace_callback_for_test(
    QsearchTtCutoffTraceCallback callback);

// Searches precisely the child reached by forced_root_move at the same
// remaining depth and ply as a normal root iteration.  This exists solely to
// prove or disprove root-score ties in the QTT acceptance harness.
SearchResult search_forced_root_move_for_test(const Board& board,
                                              const Move& forced_root_move,
                                              const SearchLimits& limits);
#endif

#if defined(HEBICHESS_NNUE_SEARCH_TEST)
// These entry points and counters exist only in the dedicated native test
// binary.  They keep the legacy full-rebuild path available for direct search
// equivalence checks without adding a production diagnostic switch.
struct NnueSearchAccumulatorCounters {
  std::uint64_t root_full_refresh_count{0};
  std::uint64_t incremental_update_count{0};
  std::uint64_t king_perspective_refresh_count{0};
  std::uint64_t eval_from_accumulator_count{0};
  std::uint64_t legacy_full_eval_count{0};
  std::uint64_t null_move_accumulator_reuse_count{0};
  std::uint64_t qsearch_incremental_update_count{0};
  std::uint64_t qsearch_eval_from_accumulator_count{0};
  // Targeted-search coverage.  These remain test-binary-only so a special
  // move can be shown to have crossed the wired accumulator boundary.
  std::uint64_t capture_incremental_update_count{0};
  std::uint64_t castling_incremental_update_count{0};
  std::uint64_t promotion_incremental_update_count{0};
  std::uint64_t en_passant_incremental_update_count{0};
};

struct NnueSearchTestResult {
  SearchResult search{};
  NnueSearchAccumulatorCounters counters{};
};

NnueSearchTestResult search_nnue_incremental_for_test(const Board& board,
                                                       const SearchLimits& limits);
NnueSearchTestResult search_nnue_legacy_for_test(const Board& board,
                                                  const SearchLimits& limits);
#endif

}  // namespace hebichess
