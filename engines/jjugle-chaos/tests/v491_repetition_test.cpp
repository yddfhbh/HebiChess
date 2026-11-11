#include <iostream>
#include <vector>
#include "chess/board.hpp"
#include "chess/movegen.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"
using namespace hebichess;

int main() {
  const auto board = Board::from_fen("4k3/8/8/8/8/8/8/4K2Q w - - 20 1");
  if (!board) { std::cerr << "FEN_INVALID\n"; return 1; }
  const auto candidate = parse_uci_move(*board, "e1f1");
  if (!candidate) { std::cerr << "CANDIDATE_ILLEGAL\n"; return 1; }
  SearchLimits base;
  base.max_depth = 2;
  base.eval_mode = EvalMode::CHAOS;
  base.use_pvs = false;
  base.use_aspiration = false;
  const auto find_score = [&](const SearchResult& r) -> int {
    for (const auto& root : r.root_moves)
      if (root.move == *candidate) return root.search_score;
    return -999999;
  };
  clear_transposition_table(); clear_search_heuristics();
  const auto normal = search(*board, base);
  const int expected_advantage = find_score(normal);
  if (normal.score < 250 || expected_advantage < 250) {
    std::cerr << "MATERIAL_ADVANTAGE_NOT_RECOGNIZED score=" << normal.score
              << " candidate=" << expected_advantage << "\n";
    return 1;
  }
  SearchLimits claim = base;
  Board probe = *board;
  probe.make_move(*candidate);
  // Two historical occurrences of the same child position, separated by
  // the correct alternating-side parity. This unit fixture is synthetic:
  // no production code depends on its FEN, piece types, or chosen move.
  claim.position_history_keys = {board->zobrist_key(), probe.zobrist_key(),
      board->zobrist_key(), probe.zobrist_key(), board->zobrist_key()};
  // A single earlier occurrence is only twofold: do not force a draw.
  SearchLimits twofold = base;
  twofold.position_history_keys = {board->zobrist_key(),
      probe.zobrist_key(), board->zobrist_key()};
  clear_transposition_table(); clear_search_heuristics();
  const auto not_drawn = search(*board, twofold);
  if (find_score(not_drawn) < 250) {
    std::cerr << "TWOFOLD_FALSE_DRAW candidate=" << find_score(not_drawn) << "\n";
    return 1;
  }
  clear_transposition_table(); clear_search_heuristics();
  const auto drawn = search(*board, claim);
  const int actual_draw = find_score(drawn);
  if (actual_draw != 0) {
    std::cerr << "THREEFOLD_NOT_SCORED_DRAW candidate=" << actual_draw
              << " best=" << drawn.score << "\n";
    return 1;
  }
  std::cout << "V491_THREEFOLD_SEARCH=PASS baseline=" << expected_advantage
            << " twofold=" << find_score(not_drawn)
            << " repeated_child=" << actual_draw << "\n";
  return 0;
}
