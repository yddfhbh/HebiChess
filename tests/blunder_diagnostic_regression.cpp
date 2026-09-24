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
                                 std::vector<NullMoveTrace>* events = nullptr) {
  SearchLimits limits;
  limits.max_depth = 10;
  limits.eval_mode = EvalMode::NNUE;
  // Match --command trace exactly: this comparison checks diagnostic
  // observability, not the objective-only sweep configuration.
  limits.use_root_style_selection = true;
  limits.use_aspiration = use_aspiration;
  limits.use_null_move = use_null_move;
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
  } catch (const std::exception& error) {
    std::cerr << "blunder diagnostic regression failure: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
