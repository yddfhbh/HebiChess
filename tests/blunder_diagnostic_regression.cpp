#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "chess/nnue.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"

using namespace hebichess;

namespace {

constexpr char kFen[] =
    "rn1qkb1r/1p2pp1p/p2p1np1/8/2PNP1b1/P1N5/1P2BPPP/R1BQK2R b KQkq - 0 1";
constexpr char kNullMoveFen[] =
    "8/2b2p2/2p3p1/p1k4p/K7/1B4P1/7P/8 b - - 0 1";
constexpr char kFirstFalseNullCutoffFen[] =
    "8/2b5/2p3B1/p6p/K7/2k3P1/7P/8 b - - 0 3";

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

const RootMoveInfo& root_info(const SearchResult& result, const Move& move) {
  const auto found = std::find_if(result.root_moves.begin(), result.root_moves.end(),
      [&move](const RootMoveInfo& item) { return item.move == move; });
  if (found == result.root_moves.end()) throw std::runtime_error("required root move is absent");
  return *found;
}

const RootMoveInfo& objective_root(const SearchResult& result) {
  const auto found = std::max_element(result.root_moves.begin(), result.root_moves.end(),
      [](const RootMoveInfo& left, const RootMoveInfo& right) {
        if (left.bound != ScoreBound::Exact) return true;
        if (right.bound != ScoreBound::Exact) return false;
        return left.search_score < right.search_score;
      });
  if (found == result.root_moves.end() || found->bound != ScoreBound::Exact)
    throw std::runtime_error("objective root is not exact");
  return *found;
}

ScoreBound expected_bound(const RootMoveInfo& info) {
  if (info.search_score <= info.search_alpha) return ScoreBound::Upper;
  if (info.search_score >= info.search_beta) return ScoreBound::Lower;
  return ScoreBound::Exact;
}

SearchResult run(const Board& board, const Move& prepared, bool reuse,
                 bool aspiration) {
  clear_transposition_table();
  clear_search_heuristics();
  SearchLimits limits;
  limits.max_depth = 8;
  limits.eval_mode = EvalMode::NNUE;
  limits.use_aspiration = aspiration;
  if (reuse) {
    limits.reuse_hit = true;
    limits.has_prepared_root_move = true;
    limits.prepared_root_move = prepared;
    limits.reuse_previous_depth = 8;
  }
  return search(board, limits);
}

SearchResult run_endgame_primary(const Board& board, bool use_aspiration,
                                 bool use_null_move, bool trace_null_moves,
                                 std::vector<NullMoveTrace>* events = nullptr,
                                 DiagnosticOracleTtMode oracle_mode =
                                     DiagnosticOracleTtMode::Seeded) {
  SearchLimits limits;
  limits.max_depth = 10;
  limits.eval_mode = EvalMode::NNUE;
  // Match --command trace exactly: this comparison checks diagnostic
  // observability, not the objective-only sweep configuration.
  limits.use_root_style_selection = true;
  limits.use_aspiration = use_aspiration;
  limits.use_null_move = use_null_move;
  limits.diagnostic_oracle_tt_mode = oracle_mode;
  if (oracle_mode == DiagnosticOracleTtMode::Clean ||
      oracle_mode == DiagnosticOracleTtMode::Counterfactual)
    limits.null_oracle_filter_event_ids.push_back("86a164571c4df2ca");
  clear_transposition_table();
  clear_search_heuristics();
  // Each paired run starts with no thread-local callback installed.
  set_null_move_trace_callback_for_diagnostic({});
  if (trace_null_moves) {
    require(events != nullptr, "traced endgame run requires an event sink");
    set_null_move_trace_callback_for_diagnostic(
        [events](const NullMoveTrace& trace) { events->push_back(trace); });
  }
  try {
    const SearchResult result = search(board, limits);
    if (trace_null_moves) set_null_move_trace_callback_for_diagnostic({});
    return result;
  } catch (...) {
    if (trace_null_moves) set_null_move_trace_callback_for_diagnostic({});
    throw;
  }
}

void require_same_primary_result(const SearchResult& ordinary,
                                 const SearchResult& traced) {
  require(ordinary.best_move == traced.best_move,
          "null trace changed selected best move");
  require(ordinary.score == traced.score,
          "null trace changed selected score");
  require(ordinary.completed_depth == traced.completed_depth,
          "null trace changed completed depth");
  require(ordinary.nodes == traced.nodes,
          "null trace changed primary nodes");
  require(ordinary.qnodes == traced.qnodes,
          "null trace changed primary qnodes");
  require(ordinary.aspiration_retries == traced.aspiration_retries,
          "null trace changed aspiration retries");
  require(ordinary.principal_variation == traced.principal_variation,
          "null trace changed primary PV");
  const RootMoveInfo& ordinary_objective = objective_root(ordinary);
  const RootMoveInfo& traced_objective = objective_root(traced);
  require(ordinary_objective.move == traced_objective.move &&
              ordinary_objective.search_score == traced_objective.search_score,
          "null trace changed objective root result");
  require(ordinary.root_moves.size() == traced.root_moves.size(),
          "null trace changed root candidate count");
  for (std::size_t index = 0; index < ordinary.root_moves.size(); ++index) {
    const RootMoveInfo& left = ordinary.root_moves[index];
    const RootMoveInfo& right = traced.root_moves[index];
    require(left.move == right.move, "null trace changed root move order");
    require(left.search_score == right.search_score && left.bound == right.bound,
            "null trace changed root candidate score or bound");
  }
}

void test_null_trace_observational_isolation() {
  const auto board = Board::from_fen(kNullMoveFen);
  require(board.has_value(), "invalid null-trace isolation FEN");
  struct Controls {
    bool aspiration;
    bool null_move;
  };
  constexpr Controls controls[] = {{true, true}, {false, true}, {true, false}};
  for (const Controls& control : controls) {
    const SearchResult ordinary =
        run_endgame_primary(*board, control.aspiration, control.null_move, false);
    std::vector<NullMoveTrace> events;
    const SearchResult traced = run_endgame_primary(
        *board, control.aspiration, control.null_move, true, &events);
    require_same_primary_result(ordinary, traced);
    if (control.null_move) {
      require(!events.empty(), "null trace did not observe any null-move attempt");
    } else {
      require(events.empty(), "null-disabled search emitted a null trace event");
    }
  }
  const SearchResult ordinary =
      run_endgame_primary(*board, true, true, false);
  std::vector<NullMoveTrace> clean_events;
  const SearchResult clean_traced = run_endgame_primary(
      *board, true, true, true, &clean_events, DiagnosticOracleTtMode::Clean);
  require_same_primary_result(ordinary, clean_traced);
  const auto causal = std::find_if(clean_events.begin(), clean_events.end(),
      [](const NullMoveTrace& event) {
        return event.event_id == "86a164571c4df2ca";
      });
  require(causal != clean_events.end() && causal->oracle_score == 727 &&
              causal->oracle_tt_mode == "clean",
          "filtered CLEAN oracle trace must prove event 86 without perturbing primary search");
  std::vector<NullMoveTrace> counterfactual_events;
  const SearchResult counterfactual_traced = run_endgame_primary(
      *board, true, true, true, &counterfactual_events,
      DiagnosticOracleTtMode::Counterfactual);
  require_same_primary_result(ordinary, counterfactual_traced);
  const auto counterfactual = std::find_if(counterfactual_events.begin(),
      counterfactual_events.end(), [](const NullMoveTrace& event) {
        return event.event_id == "86a164571c4df2ca";
      });
  require(counterfactual != counterfactual_events.end() &&
              counterfactual->oracle_tt_mode == "counterfactual" &&
              counterfactual->oracle_score == 727 &&
              counterfactual->oracle_returned_bound == "upper" &&
              counterfactual->oracle_initial_tt_hit &&
              counterfactual->tt_move_ordering_only &&
              !counterfactual->oracle_initial_tt_caused_cutoff,
          "COUNTERFACTUAL must retain TT move/history context but reject event 86 without TT bounds");
}

void test_endgame_null_move_oracle() {
  const auto board = Board::from_fen(kNullMoveFen);
  require(board.has_value(), "invalid null-move endgame FEN");
  const auto c5d4 = parse_uci_move(*board, "c5d4");
  require(c5d4.has_value(), "c5d4 must be legal");
  SearchLimits limits;
  limits.max_depth = 10;
  limits.eval_mode = EvalMode::NNUE;
  limits.use_root_style_selection = false;
  std::vector<NullMoveTrace> events;
  set_null_move_trace_callback_for_diagnostic(
      [&events](const NullMoveTrace& trace) { events.push_back(trace); });
  clear_transposition_table();
  clear_search_heuristics();
  const SearchResult result = search(*board, limits);
  set_null_move_trace_callback_for_diagnostic({});
  require(result.best_move == *c5d4 && result.score == 681,
          "normal endgame control must retain the reproduced c5d4 result");
  const auto first = std::find_if(events.begin(), events.end(),
      [&c5d4](const NullMoveTrace& trace) {
        return trace.has_root_move && trace.root_move == *c5d4 && trace.false_cutoff;
      });
  require(first != events.end(), "c5d4 subtree must contain a false null cutoff");
  require(first->fen == kFirstFalseNullCutoffFen && first->ply == 4 &&
              first->depth == 3 && first->alpha == 727 && first->beta == 728 &&
              first->reduction == 2 && first->null_score == 745 &&
              first->oracle_score.has_value() && *first->oracle_score == 727,
          "first c5d4 false null cutoff must preserve its FEN/window/oracle proof");
  require(first->tt_probe_hit && first->tt_entry_depth == 2 &&
              first->tt_bound == "exact" && first->tt_score_cp == 752 &&
              first->caller_alpha == 727 && first->caller_beta == 728 &&
              first->effective_alpha == 727 && first->effective_beta == 728 &&
              !first->tt_window_changed && first->tt_move_present &&
              first->tt_move_ordering_only && !first->tt_would_cutoff,
          "event 86 must report its shallow TT entry as ordering-only, not window influence");
  require(first->oracle_tt_mode == "seeded" && first->oracle_initial_tt_hit &&
              first->oracle_initial_tt_entry_depth == 2 &&
              first->oracle_initial_tt_bound == "exact" &&
              first->oracle_initial_tt_score == 752 &&
              !first->oracle_initial_tt_caused_cutoff && first->oracle_nodes > 0,
          "seeded oracle provenance must distinguish a shallow non-cutting TT hit");
}

void test_clean_null_free_oracle_modes() {
  const auto board = Board::from_fen(kFirstFalseNullCutoffFen);
  require(board.has_value(), "invalid clean-oracle causal position");
  clear_transposition_table();
  clear_search_heuristics();
  const DiagnosticOracleRunResult counterfactual = run_null_free_oracle_for_diagnostic(
      *board, 3, 727, 728, EvalMode::NNUE,
      DiagnosticOracleTtMode::Counterfactual, 4);
  const DiagnosticOracleRunResult clean = run_null_free_oracle_for_diagnostic(
      *board, 3, 727, 728, EvalMode::NNUE, DiagnosticOracleTtMode::Clean, 4);
  clear_transposition_table();
  const DiagnosticOracleRunResult no_tt = run_null_free_oracle_for_diagnostic(
      *board, 3, 727, 728, EvalMode::NNUE, DiagnosticOracleTtMode::CleanNoTt, 4);
  require(counterfactual.score == 727 && counterfactual.bound == ScoreBound::Upper &&
              !counterfactual.reaches_beta && counterfactual.nodes > 0 &&
              counterfactual.tt_cutoffs == 0 &&
              clean.score == 727 && clean.bound == ScoreBound::Upper &&
              !clean.reaches_beta && clean.nodes > 0 && clean.tt_probes > 0,
          "COUNTERFACTUAL and CLEAN oracles must reject event 86 without reusable TT bounds");
  require(no_tt.score == clean.score && no_tt.bound == clean.bound &&
              !no_tt.reaches_beta && no_tt.nodes > 0 && no_tt.tt_probes == 0 &&
              no_tt.tt_hits == 0 && no_tt.tt_cutoffs == 0,
          "clean-no-TT oracle must independently agree on causal event 86");

  const DiagnosticOracleRunResult counterfactual_deep = run_null_free_oracle_for_diagnostic(
      *board, 8, 534, 569, EvalMode::NNUE,
      DiagnosticOracleTtMode::Counterfactual, 4);
  const DiagnosticOracleRunResult clean_deep = run_null_free_oracle_for_diagnostic(
      *board, 8, 534, 569, EvalMode::NNUE, DiagnosticOracleTtMode::Clean, 4);
  const DiagnosticOracleRunResult no_tt_deep = run_null_free_oracle_for_diagnostic(
      *board, 8, 534, 569, EvalMode::NNUE, DiagnosticOracleTtMode::CleanNoTt, 4);
  require(counterfactual_deep.score == 534 &&
              counterfactual_deep.bound == ScoreBound::Upper &&
              !counterfactual_deep.reaches_beta &&
              clean_deep.score == 534 && clean_deep.bound == ScoreBound::Upper &&
              !clean_deep.reaches_beta && no_tt_deep.score == clean_deep.score &&
              no_tt_deep.bound == clean_deep.bound && !no_tt_deep.reaches_beta,
          "clean oracle modes must agree on the depth-8 causal occurrence");
}

void test_root_candidate_verification_is_isolated() {
  const auto board = Board::from_fen(kNullMoveFen);
  require(board.has_value(), "invalid root-verification fixture FEN");
  SearchLimits limits;
  limits.max_depth = 6;
  limits.eval_mode = EvalMode::NNUE;
  limits.use_root_style_selection = false;
  clear_transposition_table();
  clear_search_heuristics();
  const SearchResult before = search(*board, limits);
  require(before.best_move.from.is_valid() && before.best_move.to.is_valid(),
          "root-verification fixture must have a best move");
  const RootCandidateVerificationResult verified =
      verify_root_candidate_null_free_for_diagnostic(
          *board, before.best_move, before.completed_depth, EvalMode::NNUE);
  require(verified.nodes > 0 && verified.qnodes > 0,
          "root counterfactual must perform an isolated search");

  clear_transposition_table();
  clear_search_heuristics();
  const SearchResult after = search(*board, limits);
  require(before.best_move == after.best_move && before.score == after.score &&
              before.completed_depth == after.completed_depth &&
              before.nodes == after.nodes && before.qnodes == after.qnodes &&
              before.aspiration_retries == after.aspiration_retries &&
              before.root_moves.size() == after.root_moves.size(),
          "root counterfactual must not leak TT/history state into primary search");
  for (std::size_t index = 0; index < before.root_moves.size(); ++index) {
    const RootMoveInfo& left = before.root_moves[index];
    const RootMoveInfo& right = after.root_moves[index];
    require(left.move == right.move && left.search_score == right.search_score &&
                left.bound == right.bound && left.search_alpha == right.search_alpha &&
                left.search_beta == right.search_beta,
            "root counterfactual must preserve primary root order and candidate results");
  }
}

void test_shallow_nmp_diagnostic_policies() {
  const auto board = Board::from_fen(kNullMoveFen);
  require(board.has_value(), "invalid shallow NMP policy FEN");
  constexpr DiagnosticNullMovePolicy policies[] = {
      DiagnosticNullMovePolicy::SkipNoHeavyTwoMinorsDepthThree,
      DiagnosticNullMovePolicy::SkipNoHeavyTwoMinorsDepthFour,
      DiagnosticNullMovePolicy::SkipNoHeavyTwoMinorsDepthFive,
  };
  for (const DiagnosticNullMovePolicy policy : policies) {
    SearchLimits limits;
    limits.max_depth = 4;
    limits.eval_mode = EvalMode::NNUE;
    limits.use_root_style_selection = false;
    limits.null_move_diagnostic_policy = policy;
    clear_transposition_table();
    clear_search_heuristics();
    const SearchResult result = search(*board, limits);
    require(result.null_policy_skips > 0,
            "shallow diagnostic NMP policy must skip eligible low-material nodes");
  }
}

void test_single_false_cutoff_shadow() {
  constexpr char kTargetEventId[] = "86a164571c4df2ca";
  const auto board = Board::from_fen(kNullMoveFen);
  require(board.has_value(), "invalid null-shadow FEN");
  SearchLimits limits;
  limits.max_depth = 10;
  limits.eval_mode = EvalMode::NNUE;
  limits.use_root_style_selection = false;
  // The occurrence suffix makes this a true singleton even if an identical
  // transposition/window is reached more than once in the same root search.
  limits.null_shadow_event_ids.push_back(std::string(kTargetEventId) + "@1");
  clear_transposition_table();
  clear_search_heuristics();
  const SearchResult shadowed = search(*board, limits);
  require(shadowed.null_shadow_matches == 1 &&
              shadowed.null_shadow_suppressions == 1,
          "selected stable event must be encountered and suppressed once");
  require(shadowed.null_shadow_propagation_event_id == kTargetEventId &&
              shadowed.null_shadow_propagation.size() >= 2,
          "shadow must record the event's score propagation to its root branch");
  const auto c5d4 = parse_uci_move(*board, "c5d4");
  require(c5d4.has_value(), "c5d4 must be legal in null-shadow fixture");
  require(shadowed.best_move != *c5d4,
          "shadowing the causal depth-10 cutoff must change the objective move");
}

void test_occurrence_scoped_shadow_root_causality() {
  constexpr char kRootFen[] =
      "8/4k3/8/2p4p/8/4K3/7b/8 w - - 12 36";
  constexpr char kEventId[] = "45604831059b2762";
  const auto board = Board::from_fen(kRootFen);
  require(board.has_value(), "invalid occurrence-shadow root FEN");
  const auto e3f2 = parse_uci_move(*board, "e3f2");
  const auto e3d3 = parse_uci_move(*board, "e3d3");
  require(e3f2.has_value() && e3d3.has_value(), "occurrence-shadow moves must be legal");
  auto run = [&](bool shadow_second, std::vector<NullMoveTrace>* events) {
    SearchLimits limits;
    limits.max_depth = 6;
    limits.eval_mode = EvalMode::NNUE;
    limits.use_root_style_selection = false;
    limits.diagnostic_oracle_tt_mode = DiagnosticOracleTtMode::Counterfactual;
    limits.null_oracle_filter_event_ids.push_back(kEventId);
    if (shadow_second)
      limits.null_shadow_event_ids.push_back(std::string(kEventId) + "@2");
    clear_transposition_table();
    clear_search_heuristics();
    set_null_move_trace_callback_for_diagnostic({});
    if (events != nullptr)
      set_null_move_trace_callback_for_diagnostic(
          [events](const NullMoveTrace& event) { events->push_back(event); });
    SearchResult result = search(*board, limits);
    set_null_move_trace_callback_for_diagnostic({});
    return result;
  };
  const SearchResult baseline = run(false, nullptr);
  std::vector<NullMoveTrace> events;
  const SearchResult second_suppressed = run(true, &events);
  require(baseline.best_move == *e3f2 && baseline.score == -711,
          "unsuppressed corpus root must retain e3f2 at -711");
  require(second_suppressed.best_move == *e3d3 &&
              second_suppressed.null_shadow_suppressions == 1,
          "suppressing only duplicate event occurrence 2 must select e3d3");
  std::vector<const NullMoveTrace*> matching;
  for (const auto& event : events)
    if (event.event_id == kEventId) matching.push_back(&event);
  require(matching.size() == 2 && matching[0]->event_occurrence == 1 &&
              matching[1]->event_occurrence == 2 && matching[0]->false_cutoff &&
              matching[1]->false_cutoff && matching[0]->oracle_score == 710 &&
              matching[1]->oracle_score == 710,
          "duplicate robust-false events must expose stable per-search occurrence ids");
}

void test_diagnostic_tt_modes() {
  const auto board = Board::from_fen(kNullMoveFen);
  require(board.has_value(), "invalid diagnostic TT mode FEN");
  SearchLimits limits;
  limits.max_depth = 5;
  limits.eval_mode = EvalMode::HCE;

  clear_transposition_table();
  clear_search_heuristics();
  limits.diagnostic_tt_mode = DiagnosticTtMode::Normal;
  (void)search(*board, limits);

  clear_search_heuristics();
  limits.diagnostic_tt_mode = DiagnosticTtMode::ReadOnly;
  const SearchResult read_only = search(*board, limits);
  require(read_only.tt_hits > 0 && read_only.tt_cutoffs > 0 &&
              read_only.tt_stores == 0,
          "read-only mode must consume seeded TT entries without stores");

  clear_transposition_table();
  clear_search_heuristics();
  limits.diagnostic_tt_mode = DiagnosticTtMode::WriteOnly;
  const SearchResult write_only = search(*board, limits);
  require(write_only.tt_probes == 0 && write_only.tt_hits == 0 &&
              write_only.tt_cutoffs == 0 && write_only.tt_stores > 0,
          "write-only mode must store without probing the TT");

  clear_transposition_table();
  clear_search_heuristics();
  limits.diagnostic_tt_mode = DiagnosticTtMode::MoveHintsOnly;
  const SearchResult move_hints = search(*board, limits);
  require(move_hints.tt_hits > 0 && move_hints.tt_cutoffs == 0,
          "move-hints-only mode must use TT moves without bound cutoffs");

  clear_transposition_table();
  clear_search_heuristics();
  limits.diagnostic_tt_mode = DiagnosticTtMode::BoundsOnly;
  const SearchResult bounds_only = search(*board, limits);
  require(bounds_only.tt_cutoffs > 0,
          "bounds-only mode must permit TT bound cutoffs");

  clear_transposition_table();
  clear_search_heuristics();
  limits.diagnostic_tt_mode = DiagnosticTtMode::Disabled;
  const SearchResult disabled = search(*board, limits);
  require(disabled.tt_probes == 0 && disabled.tt_hits == 0 &&
              disabled.tt_cutoffs == 0 && disabled.tt_stores == 0,
          "disabled mode must not access the main TT");
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    if (argc != 3 || std::string(argv[1]) != "--network")
      throw std::runtime_error("usage: HebiChessBlunderDiagnosticRegression --network model.hebinnue");
    std::string error;
    require(load_nnue_network(argv[2], error), error.c_str());
    const auto board = Board::from_fen(kFen);
    require(board.has_value(), "invalid reconstructed FEN");
    const auto prepared = parse_uci_move(*board, "d8a5");
    const auto expected = parse_uci_move(*board, "g4e2");
    require(prepared.has_value() && expected.has_value(), "fixture moves must be legal");

    // B: ordinary fresh root ordering is unchanged.
    const SearchResult fresh = run(*board, *prepared, false, true);
    require(fresh.best_move == *expected, "fresh NNUE search must choose g4e2");
    require(objective_root(fresh).move == *expected, "fresh objective move must be g4e2");

    // A/C: the injected first root move is a fail-low in its actual
    // aspiration window, so it must be Upper and cannot be style-safe.
    const SearchResult reuse_on = run(*board, *prepared, true, true);
    const RootMoveInfo& qa5 = root_info(reuse_on, *prepared);
    require(!reuse_on.root_moves.empty() && reuse_on.root_moves.front().move == *prepared,
            "prepared move must be searched first");
    require(qa5.search_score <= qa5.search_alpha,
            "prepared first move must fail low in the aspiration window");
    require(qa5.bound == ScoreBound::Upper,
            "first root full-window fail-low must be Upper");
    require(!qa5.style_safe, "fail-low prepared move must not be style-safe");
    require(objective_root(reuse_on).move == *expected,
            "reuse objective move must remain g4e2");
    require(reuse_on.best_move == *expected, "reuse final move must remain g4e2");

    // D: g4e2 reaches a PVS full re-search; its recorded result must be
    // classified from that full-search window, not from the zero window.
    const RootMoveInfo& ge2 = root_info(reuse_on, *expected);
    require(ge2.pvs_full_research, "g4e2 must receive a PVS full re-search");
    require(ge2.bound == expected_bound(ge2),
            "PVS full re-search bound must match its saved root window");

    // A: disabling aspiration must agree on final correctness.
    const SearchResult aspiration_off = run(*board, *prepared, true, false);
    require(objective_root(aspiration_off).move == *expected,
            "aspiration-off objective move must be g4e2");
    require(aspiration_off.best_move == *expected,
            "aspiration-on/off final moves must agree");
    test_null_trace_observational_isolation();
    test_endgame_null_move_oracle();
    test_clean_null_free_oracle_modes();
    test_root_candidate_verification_is_isolated();
    test_single_false_cutoff_shadow();
    test_occurrence_scoped_shadow_root_causality();
    test_shallow_nmp_diagnostic_policies();
    test_diagnostic_tt_modes();
  } catch (const std::exception& error) {
    std::cerr << "blunder diagnostic regression failure: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
