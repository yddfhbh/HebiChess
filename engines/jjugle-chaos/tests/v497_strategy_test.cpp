#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "chess/board.hpp"
#include "chess/eval.hpp"
#include "chess/movegen.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"

using namespace hebichess;

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "v497 strategy regression failure: " << message << '\n';
    std::exit(1);
  }
}

void play(Board& board, std::vector<ZobristKey>& history, const char* uci) {
  const auto move = parse_uci_move(board, uci);
  if (!move) {
    std::cerr << "ILLEGAL_TEST_MOVE=" << uci << " FEN=" << board.to_fen() << '\n';
    std::exit(2);
  }
  board.make_move(*move);
  history.push_back(board.zobrist_key());
}

void test_minor_coordination_and_material_imbalance() {
  const auto active = Board::from_fen(
      "q3k3/8/8/8/2BN4/5N2/PPPPPPPP/4K3 w - - 0 1");
  const auto undeveloped = Board::from_fen(
      "q3k3/8/8/8/8/8/PPPPPPPP/1NB1K1N1 w - - 0 1");
  require(active.has_value() && undeveloped.has_value(), "coordination FEN parse");

  const int active_coord = evaluate_chaos_minor_coordination(*active, Color::White);
  const int undeveloped_coord = evaluate_chaos_minor_coordination(*undeveloped, Color::White);
  require(active_coord > undeveloped_coord,
          "active bishop/knight group must outscore undeveloped minors");
  require(active_coord > 0, "active minor coordination bonus must be positive");
  require(evaluate_chaos_minor_coordination(*active, Color::Black) == -active_coord,
          "minor coordination must be zero-sum");

  // Queen versus three minors: the material term already sees +90cp for the
  // minor side; V497 adds only a bounded, activity-dependent composition bonus.
  const int q_vs_three_minors =
      evaluate_chaos_material_imbalance(*active, Color::White);
  require(q_vs_three_minors > 0,
          "three coordinated minors versus a queen must receive a premium");
  require(evaluate_chaos_material_imbalance(*active, Color::Black) ==
              -q_vs_three_minors,
          "material-imbalance premium must be zero-sum");
  require(q_vs_three_minors <= 28, "material-imbalance premium must remain bounded");

  // Rook+minor versus queen is close in raw material; reward only a modest
  // amount when the minor is supported by an active formation.
  const auto rook_minor_vs_queen = Board::from_fen(
      "q3k3/8/8/8/2B5/8/8/4K2R w - - 0 1");
  require(rook_minor_vs_queen.has_value(), "rook+minor FEN parse");
  require(evaluate_chaos_material_imbalance(*rook_minor_vs_queen, Color::White) > 0,
          "rook+minor versus queen must be recognized as a viable imbalance");

  // Bishop+knight against a rook already has the correct material score;
  // V497 should recognize the coordination potential without changing HCE.
  const auto minors_vs_rook = Board::from_fen(
      "r3k3/8/8/8/2BN4/8/8/4K3 w - - 0 1");
  require(minors_vs_rook.has_value(), "bishop+knight versus rook FEN parse");
  require(evaluate_chaos_material_imbalance(*minors_vs_rook, Color::White) > 0,
          "bishop+knight versus rook must receive a bounded coordination premium");
  require(evaluate_material(*minors_vs_rook, Color::White) == 150,
          "standard material accounting must remain unchanged");

  // HCE is deliberately untouched by all V497 CHAOS-only features.
  const auto initial = Board::initial();
  require(evaluate_hce(initial) == 10, "HCE start-position regression");
}

void test_desperation_defense_keeps_objective_best_without_draw() {
  const auto parsed = Board::from_fen("1n2k2q/8/8/8/8/8/8/1N2K3 w - - 0 1");
  require(parsed.has_value(), "no-draw desperation FEN parse");
  SearchLimits limits;
  limits.eval_mode = EvalMode::CHAOS;
  limits.max_depth = 3;
  clear_transposition_table();
  clear_search_heuristics();
  const SearchResult result = search(*parsed, limits);
  require(result.v497_desperation_defense,
          "lost position must disable style-driven speculative attack");
  require(result.v497_defense_reason == "best_defense",
          "without an immediate draw, keep the objective best defense");
}

void test_desperation_defense_prefers_available_threefold() {
  // White is materially lost (queen+knight versus a lone knight), but can
  // claim a threefold by repeating Nb1-c3.  Build the actual two-cycle history
  // rather than injecting synthetic repetition keys.
  auto parsed = Board::from_fen("1n2k2q/8/8/8/8/8/8/1N2K3 w - - 0 1");
  require(parsed.has_value(), "desperation FEN parse");
  Board board = *parsed;
  std::vector<ZobristKey> history{board.zobrist_key()};
  const std::vector<const char*> cycle = {
      "b1c3", "b8c6", "c3b1", "c6b8",
      "b1c3", "b8c6", "c3b1", "c6b8"};
  for (const char* move : cycle) play(board, history, move);

  require(board.zobrist_key() == parsed->zobrist_key(), "two knight cycles must restore root position");
  require(evaluate_chaos(board) <= -350,
          "desperation fixture must be clearly losing by static evaluation");

  SearchLimits limits;
  limits.eval_mode = EvalMode::CHAOS;
  limits.max_depth = 4;
  limits.position_history_keys = history;
  clear_transposition_table();
  clear_search_heuristics();
  const SearchResult result = search(board, limits);
  std::cout << "DESPERATION_BEST=" << move_to_uci(result.best_move)
            << " SCORE=" << result.score
            << " MODE=" << result.v497_desperation_defense
            << " REASON=" << result.v497_defense_reason << '\n';

  const auto repeat = parse_uci_move(board, "b1c3");
  require(repeat.has_value(), "threefold move must be legal");
  require(result.v497_desperation_defense,
          "desperation-defense policy must activate in a lost position");
  require(result.v497_defense_reason == "threefold",
          "available threefold must be identified as the defensive resource");
  require(result.best_move == *repeat,
          "engine must prefer the available threefold over a speculative attack");
}

}  // namespace

int main() {
  test_minor_coordination_and_material_imbalance();
  test_desperation_defense_keeps_objective_best_without_draw();
  test_desperation_defense_prefers_available_threefold();
  std::cout << "V497_MATERIAL_IMBALANCE_AND_DESPERATION=PASS\n";
  return 0;
}
