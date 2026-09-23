#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

#include "chess/nnue.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"

using namespace hebichess;

namespace {

constexpr char kFen[] =
    "rn1qkb1r/1p2pp1p/p2p1np1/8/2PNP1b1/P1N5/1P2BPPP/R1BQK2R b KQkq - 0 1";

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
  } catch (const std::exception& error) {
    std::cerr << "blunder diagnostic regression failure: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
