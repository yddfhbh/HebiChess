#include <cstdlib>
#include <iostream>
#include <string>
#include "chess/board.hpp"
#include "chess/eval.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"
using namespace hebichess;
namespace {
void require(bool condition, const char* reason) {
  if (!condition) { std::cerr << "V498_FAIL=" << reason << '\n'; std::exit(1); }
}
Move move(const Board& board, const char* uci) {
  const auto m = parse_uci_move(board, uci);
  require(m.has_value(), "fixture legal move");
  return *m;
}
SearchResult fixed_search(const Board& board, int depth) {
  SearchLimits limits;
  limits.max_depth = depth;
  limits.eval_mode = EvalMode::CHAOS;
  clear_transposition_table();
  clear_search_heuristics();
  return search(board, limits);
}
void test_conversion_cap() {
  require(chaos_conversion_style_cap_cp(0) == 125, "equal-fighting-spirit-preserved");
  require(chaos_conversion_style_cap_cp(-100) == 125, "losing-fighting-spirit-preserved");
  require(chaos_conversion_style_cap_cp(40) <= 40, "small-lead-guard");
  require(chaos_conversion_style_cap_cp(142) <= 12, "rook-trade-lead-guard");
  require(chaos_conversion_style_cap_cp(350) <= 5, "clear-win-guard");
  require(chaos_conversion_style_cap_cp(800) == 0, "decisive-win-guard");
}
void test_vietnamese_coffee_38() {
  // Real Lichess game ZrPyxvyd immediately before 38.Rc4+?? Rxc4 39.bxc4:
  // Stockfish's advantage went from +1.74 to 0.00 after the rook trade.
  auto p = Board::from_fen("8/1p6/2kp2R1/8/P6R/1P5p/2r5/3n3K w - - 0 38");
  require(p.has_value(), "game ZrPyxvyd FEN");
  require(chaos_voluntary_endgame_rook_trade(*p, move(*p,"h4c4")),
          "classify unsafe rook liquidation from real game");
  require(!chaos_voluntary_endgame_rook_trade(*p, move(*p,"h1g1")),
          "keep-king-move eligible");
  const SearchResult result = fixed_search(*p,4);
  std::cout << "ZRPY_38_V498=" << move_to_uci(result.best_move)
            << " score=" << result.score << " cap=" << result.v498_style_loss_cap_cp << '\n';
  require(result.score >= 100, "fixture has root advantage at depth 4");
  require(result.best_move != move(*p,"h4c4"),
          "no voluntary rook liquidation of search advantage");
}
void test_t4_early_queen_preservation() {
  // VietnameseCoffee vs JJUGLE, t4WwYxQ8, immediately after 7.c3.
  // A queen trade appeared in the live game; the fixed-depth position has a
  // playable bishop retreat, keeping queens and tension without losing a queen.
  const auto p = Board::from_fen(
      "r2qk1nr/ppp2ppp/2n5/8/1b2N1b1/2P2N2/PP2PPPP/R1BQKB1R b KQkq - 0 7");
  require(p.has_value(), "game t4WwYxQ8 FEN");
  const SearchResult result = fixed_search(*p, 4);
  std::cout << "T4_7_V498=" << move_to_uci(result.best_move) << '\n';
  require(result.best_move != move(*p,"d8d1"),
          "avoid premature queen exchange if a safe alternative exists");
}

void test_ottx_44_minor_gift() {
  // Real game OtTxOkSP immediately before 44.Bxa7+?? Kxa7 (score -1 -> -5.5).
  auto p = Board::from_fen("rk6/ppr1bp1q/3p1n2/1Q2p3/PP4P1/1NPRB2P/7K/6R1 w - - 2 44");
  require(p.has_value(), "game OtTxOkSP FEN");
  require(chaos_minor_pawn_king_gift(*p,move(*p,"e3a7")),
          "classify minor gift to enemy king");
  require(!chaos_minor_pawn_king_gift(*p,move(*p,"g4g5")),
          "quiet defense remains eligible");
  const SearchResult result = fixed_search(*p,3);
  std::cout << "OTTX_44_V498=" << move_to_uci(result.best_move)
            << " score=" << result.score << '\n';
  require(result.best_move != move(*p,"e3a7"),
          "no stylistic one-pawn-for-bishop gift");
}
void test_good_imbalance_not_suppressed() {
  // Queen-for-three-minors / rook-for-minor+minor remain objective-search
  // decisions, never forbidden simply because a high-value piece is lost.
  auto p=Board::from_fen("4k3/8/8/8/2bn4/5n2/8/K6Q w - - 0 1");
  require(p.has_value(),"imbalance-position");
  require(!chaos_minor_pawn_king_gift(*p,move(*p,"h1h5")),
          "queen move is not a minor gift");
}
void test_baseline_hce() {
  require(evaluate_hce(Board::initial()) == 10, "HCE unchanged");
}
}
int main() {
  test_conversion_cap();
  test_vietnamese_coffee_38();
  test_ottx_44_minor_gift();
  test_t4_early_queen_preservation();
  test_good_imbalance_not_suppressed();
  test_baseline_hce();
  std::cout << "V498_CONVERSION_LOCK=PASS\n";
}
