#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "chess/board.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"
#include "chess/uci_engine.hpp"

using namespace hebichess;
namespace {
void require(bool yes, const char* why) {
  if (!yes) { std::cerr << "V499_FAIL=" << why << '\n'; std::exit(1); }
}
struct Path {
  Board board = Board::initial();
  std::vector<Move> moves;
  std::vector<ZobristKey> keys{board.zobrist_key()};
  void push(const char* uci) {
    const auto m = parse_uci_move(board, uci);
    require(m.has_value(), "fixture move legal");
    moves.push_back(*m);
    board.make_move(*m);
    keys.push_back(board.zobrist_key());
  }
  void replay(const std::vector<const char*>& uci) {
    for (auto s : uci) push(s);
  }
  Move move(const char* uci) const {
    const auto m = parse_uci_move(board, uci);
    require(m.has_value(), "candidate legal");
    return *m;
  }
  SearchResult analyze(int depth = 4, bool game_history = true) const {
    SearchLimits limits;
    limits.max_depth = depth;
    limits.eval_mode = EvalMode::CHAOS;
    if (game_history) {
      limits.position_history_keys = keys;
      limits.recent_game_moves = moves;
    }
    clear_transposition_table();
    clear_search_heuristics();
    return search(board, limits);
  }
};

Path knight_four() {
  Path game;
  game.replay({"e2e3", "g7g6", "b1c3", "f8g7", "g1f3", "b8c6", "d2d4"});
  return game;
}
Path queen_eleven() {
  Path game;
  game.replay({
      "e2e4", "e7e5", "g1f3", "b8c6", "b1c3", "g8f6", "f3e5", "c6e5",
      "d2d4", "e5g4", "e4e5", "f8b4", "e5f6", "b4c3", "b2c3", "g4f6",
      "d1e2", "e8f8", "e2c4", "d7d6"});
  return game;
}
void test_knight_early_wasted_tempo() {
  const auto game = knight_four();
  require(chaos_opening_repeated_piece_move(game.board, game.move("c6b4"), game.moves),
          "classify unforced repeat Nb4");
  require(!chaos_opening_repeated_piece_move(game.board, game.move("d7d5"), game.moves),
          "do not flag d5 progress");
  const auto out = game.analyze(4);
  std::cout << "GAME_IOI0_4=" << move_to_uci(out.best_move)
            << " guard=" << out.v499_tempo_reason << " score=" << out.score << '\n';
  require(out.best_move != game.move("c6b4"), "avoid bookless 4..Nb4");
  require(out.v499_opening_tempo_guard && out.v499_tempo_reason == "repeated_knight",
          "root replaces style-driven Nb4 with verified objective choice");
}
void test_queen_repeated() {
  const auto queen11 = queen_eleven();
  Path first_queen_repeat;
  for (std::size_t i = 0; i < queen11.moves.size() - 2; ++i) {
    const std::string uci = move_to_uci(queen11.moves[i]);
    first_queen_repeat.push(uci.c_str());
  }
  require(chaos_opening_repeated_piece_move(first_queen_repeat.board,
                                           first_queen_repeat.move("e2c4"),
                                           first_queen_repeat.moves),
          "classify first avoidable 10.Qc4 follow-up");
  const auto earlier = first_queen_repeat.analyze(4);
  std::cout << "GAME_QD7_10=" << move_to_uci(earlier.best_move)
            << " guard=" << earlier.v499_tempo_reason << '\n';
  require(earlier.best_move != first_queen_repeat.move("e2c4") &&
          earlier.v499_opening_tempo_guard,
          "avoid first unnecessary Qc4");

  auto game = queen_eleven();
  require(chaos_opening_repeated_piece_move(game.board, game.move("c4e2"), game.moves),
          "classify Qc4 to Qe2 bounce");
  const auto first = game.analyze(4);
  std::cout << "GAME_QD7_11=" << move_to_uci(first.best_move)
            << " guard=" << first.v499_tempo_reason << '\n';
  require(first.best_move != game.move("c4e2"), "avoid 11.Qe2 bounce");
  require(first.v499_opening_tempo_guard && first.v499_tempo_reason == "repeated_queen",
          "root prefers development instead of queen bounce");

  game.push("c4e2"); game.push("c8f5");
  require(chaos_opening_repeated_piece_move(game.board, game.move("e2c4"), game.moves),
          "classify Qe2 to Qc4 bounce");
  const auto second = game.analyze(6);
  std::cout << "GAME_QD7_12=" << move_to_uci(second.best_move)
            << " guard=" << second.v499_tempo_reason << '\n';
  require(second.best_move != game.move("e2c4"), "avoid 12.Qc4 bounce");

  // A later repeat after unproductive 12.Qc4, 13.Qe2, 14.Qc4.
  game.push("e2c4"); game.push("f5c2"); game.push("c4e2");
  game.push("c2a4"); game.push("e2c4"); game.push("a4c6");
  const auto late = game.analyze(4);
  std::cout << "GAME_QD7_15=" << move_to_uci(late.best_move)
            << " guard=" << late.v499_tempo_reason << '\n';
  require(late.best_move != game.move("c4e2"), "avoid 15.Qe2 critical blunder");
  require(late.v499_opening_tempo_guard, "15 queen bounce guard fired");
}
void test_forced_escape_and_tactical_exceptions() {
  auto game = knight_four();
  require(!chaos_opening_repeated_piece_move(game.board, game.move("c6b4"), {}),
          "no game history means no tempo veto");
  game.push("c6b4"); game.push("a2a3");
  require(!chaos_opening_repeated_piece_move(game.board, game.move("b4c6"), game.moves),
          "attacked knight must be free to escape");
  auto queen = queen_eleven();
  require(!chaos_opening_repeated_piece_move(queen.board, queen.move("c4e2"), {}),
          "FEN setup without moves cannot invent repetition");
  // Future check/capture continuations must remain unrestricted even for a
  // moved queen or knight.  Opening phase and the current position gate do not
  // penalize tactics: the objective search is never replaced.
  const auto fen = Board::from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 20");
  require(fen.has_value(), "late fixture FEN");
  require(!chaos_opening_repeated_piece_move(*fen, queen.move("c4e2"), queen.moves),
          "late-game development guard disabled");
}
void test_uci_history_propagation() {
  std::vector<std::string> messages;
  UciEngine uci([&](const std::string& s){ messages.push_back(s); });
  uci.send_command("uci");
  bool version_ok = false;
  for (const auto& x : messages)
    if (x == "id name JJUGLE Chaos v4.9.12 Development Continuation") version_ok = true;
  require(version_ok, "V499 UCI ID");
  uci.send_command("position startpos moves e3e4"); // illegal must not corrupt state
  messages.clear();
  uci.send_command("position startpos moves e2e3 g7g6 b1c3 f8g7 g1f3 b8c6 d2d4");
  uci.send_command("go depth 4");
  bool guard = false, best = false;
  for (const auto& x : messages) {
    if (x == "info string v499_opening_tempo_guard repeated_knight") guard = true;
    if (x.rfind("bestmove ",0)==0 && x.rfind("bestmove c6b4",0)!=0) best = true;
  }
  require(guard && best, "UCI path passes actual moves to root guard");
}
}
int main() {
  test_knight_early_wasted_tempo();
  test_queen_repeated();
  test_forced_escape_and_tactical_exceptions();
  test_uci_history_propagation();
  std::cout << "V499_DEVELOPMENT_FIRST=PASS\n";
}
