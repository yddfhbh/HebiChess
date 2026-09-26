#pragma once

#include <cstdint>
#include <functional>
#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "chess/eval.hpp"
#include "chess/movegen.hpp"
#include "chess/search_profile.hpp"
#include "chess/tt.hpp"

#ifndef HEBICHESS_NMP_CANDIDATE_G
#define HEBICHESS_NMP_CANDIDATE_G 0
#endif

namespace hebichess {

constexpr int MATE_SCORE = 30000;

enum class ScoreBound : std::uint8_t { Exact, Lower, Upper };

// Keep exact scores, threshold proofs, and prefilter decisions distinct for
// diagnostic consumers of root-style results.
enum class StyleProofResult : std::uint8_t {
  NotRun,
  ExactScore,
  PrefilterSkipped,
  UpperBoundRejected,
  ThresholdProven,
  ThresholdRejected,
};

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
  // The actual window used to search this root move.  This is root-result
  // metadata for diagnostics and regression tests; it is never searched on.
  int search_alpha{-MATE_SCORE};
  int search_beta{MATE_SCORE};
  bool pvs_full_research{false};
  // A threshold proof is sufficient for style selection, but is not an exact
  // minimax score.  Keep that distinction explicit for callers.
  bool style_safe{false};
  int style_score{0};
  StyleProofResult style_proof{StyleProofResult::NotRun};
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

#if defined(HEBICHESS_BLUNDER_DIAGNOSTIC)
struct DiagnosticIterationSummary {
  int depth{0};
  int score{0};
  std::uint64_t nodes{0};
  std::uint64_t qnodes{0};
  std::uint64_t tt_hits{0};
  std::uint64_t tt_cutoffs{0};
  std::uint64_t tt_stores{0};
  std::vector<std::pair<int, int>> aspiration_windows{};
  std::vector<Move> root_order{};
  std::vector<RootMoveInfo> root_candidates{};
  std::vector<int> alpha_after_each_root_move{};
};
#endif

struct SearchResult {
#if HEBICHESS_SEARCH_PROFILE
  SearchProfileStats profile{};
#endif
  Move best_move{};
  int score{0};
  // Updated only after all root moves (and any aspiration retry) finish.
  // A deadline can therefore never expose a partial root iteration as final.
  int completed_depth{0};
  std::uint64_t nodes{0};
  std::uint64_t main_nodes{0};
  std::uint64_t qnodes{0};
  std::uint64_t q_stand_pat_beta_cutoffs{0};
  std::uint64_t q_in_check_nodes{0};
  std::uint64_t q_non_check_nodes{0};
  std::uint64_t q_tactical_generated{0};
  std::uint64_t q_tactical_searched{0};
  std::uint64_t q_see_pruned{0};
  std::uint64_t q_delta_pruned{0};
  std::uint64_t q_stalemate_full_movegen_calls{0};
  std::uint64_t q_max_ply{0};
  std::uint64_t q_check_evasion_generated{0};
  std::uint64_t q_check_evasion_searched{0};
  std::uint64_t q_nnue_evals{0};
  std::uint64_t q_nnue_incremental_updates{0};
  std::uint64_t q_see_calls{0};
  std::uint64_t q_gives_check_calls{0};
  std::uint64_t q_gives_check_skipped{0};
  std::uint64_t q_gives_check_for_see_exception{0};
  std::uint64_t q_gives_check_for_delta_exception{0};
  std::uint64_t q_gives_check_for_ordering{0};
  std::uint64_t q_evasion_check_ordering_calls{0};
  std::uint64_t q_full_legal_movegen_calls{0};
  std::uint64_t q_tactical_movegen_calls{0};
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
  std::uint64_t qtt_cutoffs{0};
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
#if HEBICHESS_NMP_CANDIDATE_G
  // Production-candidate G telemetry.
  std::uint64_t nmp_g_verification_attempts{0};
  std::uint64_t nmp_g_confirmed_verifications{0};
  std::uint64_t nmp_g_rejected_verifications{0};
  std::uint64_t nmp_g_verification_nodes{0};
  std::uint64_t nmp_g_verification_qnodes{0};
#endif
#if defined(HEBICHESS_BLUNDER_DIAGNOSTIC)
  // Never present in production builds. These distinguish a diagnostic
  // policy's skipped/rejected proof from an ordinary NMP cutoff.
  std::uint64_t null_policy_skips{0};
  std::uint64_t null_policy_rejected_cutoffs{0};
  std::uint64_t null_policy_verification_searches{0};
  std::uint64_t null_policy_verified_cutoffs{0};
  std::uint64_t null_policy_verification_nodes{0};
  std::uint64_t null_policy_verification_qnodes{0};
  std::uint64_t null_policy_admission_evaluations{0};
  std::uint64_t null_policy_admission_rejections{0};
  std::uint64_t null_policy_reduction_overrides{0};
  std::uint64_t null_shadow_matches{0};
  std::uint64_t null_shadow_suppressions{0};
  std::vector<std::string> null_shadow_matched_event_ids{};
  struct NullShadowPropagationStep {
    int ply{0};
    int depth{0};
    int alpha{0};
    int beta{0};
    int returned_score{0};
    ScoreBound bound{ScoreBound::Exact};
  };
  std::string null_shadow_propagation_event_id{};
  std::vector<NullShadowPropagationStep> null_shadow_propagation{};
#endif
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
  std::vector<Move> principal_variation{};
  bool reuse_hit{false};
  bool reuse_verified{false};
  bool reuse_fast_stop{false};
  std::string reuse_expected{};
  std::string reuse_prepared{};
  int reuse_prepared_depth{0};
  std::vector<RootMoveInfo> root_moves{};
#if defined(HEBICHESS_BLUNDER_DIAGNOSTIC)
  std::vector<DiagnosticIterationSummary> diagnostic_iterations{};
#endif
};

#if defined(HEBICHESS_BLUNDER_DIAGNOSTIC)
enum class DiagnosticNullMovePolicy : std::uint8_t {
  Production,
  DisableNoHeavyTwoMinors,
  DisableNoHeavySideOneMinor,
  ContinueOnLowMaterialFailHigh,
  VerifyLowMaterialFailHigh,
  VerifyLowMaterialFailHighNullFree,
  SkipNoHeavyTwoMinorsDepthSix,
  SkipNoHeavyTwoMinorsDepthThree,
  SkipNoHeavyTwoMinorsDepthFour,
  SkipNoHeavyTwoMinorsDepthFive,
  VerifyReducedLowMaterialDepthMinusReduction,
  VerifyReducedLowMaterialDepthMinusOne,
  VerifyReducedLowMaterialDepthMinusReductionPlusOne,
  VerifyNoHeavyTwoMinorsDepthSixNullFree,
  VerifyNoHeavyTwoMinorsDepthSixMargin71NullFree,
  VerifyNoHeavyTwoMinorsDepthSixWidth70NullFree,
  AdmissionStaticEval,
  AdmissionLowMaterialStaticEval,
  AdmissionLowMaterialStaticEvalMargin16,
  AdmissionLowMaterialStaticEvalMargin32,
  AdmissionLowMaterialStaticEvalMargin64,
  ReductionOneLowMaterialDepthSix,
  VerifyLowMaterialFailHighClean,
};

enum class DiagnosticTtMode : std::uint8_t {
  Normal,
  Disabled,
  ReadOnly,
  WriteOnly,
  MoveHintsOnly,
  BoundsOnly,
  ClearBetweenDepths,
  ClearBetweenRetries,
};
enum class DiagnosticNullMatchLevel : std::uint8_t {
  Exact,
  PositionDepth,
  Position,
};
enum class DiagnosticOracleTtMode : std::uint8_t {
  Seeded,
  // Same live TT/history snapshot, but TT bounds are ordering-only: the
  // counterfactual asks what this node returns without any NMP-derived TT
  // score deciding the window or cutoff.
  Counterfactual,
  Clean,
  CleanNoTt,
  MoveOnly,
};
struct DiagnosticOracleRunResult {
  int score{0};
  ScoreBound bound{ScoreBound::Exact};
  bool reaches_beta{false};
  std::uint64_t nodes{0};
  std::uint64_t qnodes{0};
  std::uint64_t tt_probes{0};
  std::uint64_t tt_hits{0};
  std::uint64_t tt_cutoffs{0};
};
struct RootCandidateVerificationResult {
  int score{0};
  ScoreBound bound{ScoreBound::Exact};
  std::uint64_t nodes{0};
  std::uint64_t qnodes{0};
  std::uint64_t elapsed_ms{0};
  std::vector<Move> pv{};
};
#endif

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
  bool reuse_hit{false};
  bool has_prepared_root_move{false};
  Move prepared_root_move{};
  int reuse_previous_depth{0};
  int reuse_verification_ms{0};
  // Diagnostic switch only: false reproduces the v2.1 root safety margin.
  bool use_style_v3{true};
  // Used by the separately built diagnostic harness for objective-only
  // experiments.  Normal UCI searches leave root style selection enabled.
  bool use_root_style_selection{true};
  // -1 selects the v3.2 adaptive reserve.  A non-negative value is intended
  // for controlled benchmarks of the root verification budget.
  int style_verification_reserve_ms{-1};
  bool profile_style_metadata{false};
  EvalMode eval_mode{EvalMode::HCE};
#if defined(HEBICHESS_BLUNDER_DIAGNOSTIC)
  // Diagnostic target only; production always follows the established NMP
  // path and does not compile this switch.
  DiagnosticNullMovePolicy null_move_diagnostic_policy{
      DiagnosticNullMovePolicy::Production};
  std::vector<std::string> null_shadow_event_ids{};
  std::vector<std::string> null_shadow_position_fens{};
  DiagnosticNullMatchLevel null_shadow_match_level{DiagnosticNullMatchLevel::Exact};
  int null_shadow_position_depth{-1};
  int null_shadow_position_reduction{-1};
  DiagnosticTtMode diagnostic_tt_mode{DiagnosticTtMode::Normal};
  DiagnosticOracleTtMode diagnostic_oracle_tt_mode{DiagnosticOracleTtMode::Seeded};
  std::vector<std::string> null_oracle_filter_event_ids{};
  int diagnostic_vclean_depth{3};
#endif
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
std::vector<Move> extract_principal_variation(const Board& board,
                                              const Move& root_move,
                                              int max_plies = 6);
void clear_transposition_table() noexcept;
void clear_search_heuristics() noexcept;

#if HEBICHESS_NMP_CANDIDATE_G
struct NmpCandidateGWindowResult {
  int score{0};
  std::uint64_t nodes{0};
  std::uint64_t qnodes{0};
  std::uint64_t verification_attempts{0};
  std::uint64_t confirmed_verifications{0};
  std::uint64_t rejected_verifications{0};
};

NmpCandidateGWindowResult search_nmp_candidate_g_window_for_test(
    const Board& board, int depth, int alpha, int beta, int ply,
    EvalMode eval_mode = EvalMode::HCE);
#endif

#if defined(HEBICHESS_BLUNDER_DIAGNOSTIC)
// Diagnostic-target-only Null Move cutoff audit. The callback is never
// compiled into an engine target and its oracle re-search never mutates TT.
struct NullMoveTrace {
  std::string fen{};
  Move root_move{};
  bool has_root_move{false};
  int ply{0};
  int depth{0};
  int alpha{0};
  int beta{0};
  int reduction{0};
  int null_score{0};
  bool cutoff{false};
  Color side_to_move{Color::White};
  std::string material{};
  std::optional<int> oracle_score{};
  bool oracle_reaches_beta{false};
  bool false_cutoff{false};
  std::uint64_t event_occurrence{0};
  std::optional<int> static_eval_cp{};
  int legal_move_count{0};
  int legal_king_moves{0};
  int legal_pawn_moves{0};
  int legal_minor_moves{0};
  int legal_non_pawn_non_king_moves{0};
  int legal_captures{0};
  int legal_quiet_moves{0};
  int pawn_advance_moves{0};
  int passed_pawns_white{0};
  int passed_pawns_black{0};
  int connected_passed_pawns_white{0};
  int connected_passed_pawns_black{0};
  int material_imbalance_cp{0};
  int stm_non_pawn_material_cp{0};
  bool stm_in_check{false};
  bool stm_has_pawn_push{false};
  bool stm_has_capture{false};
  bool tt_probe_hit{false};
  int tt_entry_depth{-1};
  std::string tt_bound{"none"};
  std::optional<int> tt_score_cp{};
  int caller_alpha{0};
  int caller_beta{0};
  int effective_alpha{0};
  int effective_beta{0};
  bool tt_raised_alpha{false};
  bool tt_lowered_beta{false};
  bool tt_window_changed{false};
  bool tt_move_present{false};
  bool tt_move_ordering_only{false};
  bool tt_would_cutoff{false};
  std::string oracle_tt_mode{"seeded"};
  bool oracle_initial_tt_hit{false};
  int oracle_initial_tt_entry_depth{-1};
  std::string oracle_initial_tt_bound{"none"};
  std::optional<int> oracle_initial_tt_score{};
  bool oracle_initial_tt_caused_cutoff{false};
  std::uint64_t oracle_tt_probes{0};
  std::uint64_t oracle_tt_hits{0};
  std::uint64_t oracle_tt_cutoffs{0};
  std::uint64_t oracle_nodes{0};
  std::uint64_t oracle_qnodes{0};
  std::string oracle_returned_bound{"unknown"};
  std::string event_id{};
};

struct NullMoveVerificationTrace {
  std::string fen{};
  Move root_move{};
  bool has_root_move{false};
  std::string event_id{};
  int ply{0};
  int depth{0};
  int alpha{0};
  int beta{0};
  int reduction{0};
  int null_score{0};
  int verification_depth{0};
  int verification_score{0};
  bool accepted{false};
  std::uint64_t verification_nodes{0};
  std::uint64_t verification_qnodes{0};
  std::optional<int> full_oracle_score{};
};

using NullMoveTraceCallback = std::function<void(const NullMoveTrace&)>;
void set_null_move_trace_callback_for_diagnostic(NullMoveTraceCallback callback);
using NullMoveVerificationTraceCallback =
    std::function<void(const NullMoveVerificationTrace&)>;
void set_null_move_verification_trace_callback_for_diagnostic(
    NullMoveVerificationTraceCallback callback);
DiagnosticOracleRunResult run_null_free_oracle_for_diagnostic(
    const Board& board, int depth, int alpha, int beta, EvalMode eval_mode,
    DiagnosticOracleTtMode tt_mode, int ply = 0);
RootCandidateVerificationResult verify_root_candidate_null_free_for_diagnostic(
    const Board& board, const Move& root_move, int root_depth, EvalMode eval_mode);
SearchResult search_forced_root_move_for_null_diagnostic(
    const Board& board, const Move& forced_root_move, const SearchLimits& limits);
#endif

#if defined(HEBICHESS_SEARCH_TT_WINDOW_TEST)
// Test-only harness for exercising the main negamax TT probe/store path with
// a deliberately preloaded bound.  It is not compiled into engine targets.
struct TtWindowStoreTestResult {
  int score{0};
  TTEntry stored{};
  std::uint64_t tt_hits{0};
};

TtWindowStoreTestResult search_with_preloaded_tt_bound_for_test(
    Board board, int depth, int alpha, int beta, int injected_score,
    TTBound injected_bound);
#endif

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
