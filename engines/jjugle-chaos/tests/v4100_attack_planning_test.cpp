#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include "chess/board.hpp"
#include "chess/movegen.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"
#include "chess/uci_engine.hpp"
using namespace hebichess;
namespace {
void require(bool yes, const char* issue) {
  if (!yes) {std::cerr << "V4100_FAILURE=" << issue << '\n';std::exit(1);}
}
SearchResult analyze(const char* fen, int depth) {
  auto board = Board::from_fen(fen);
  require(board.has_value(), "fixture FEN parses");
  SearchLimits limits;
  limits.max_depth = depth;
  limits.eval_mode = EvalMode::CHAOS;
  clear_transposition_table();
  clear_search_heuristics();
  return search(*board, limits);
}
void test_rook_lift_shape() {
  // tckafQbA, immediately before 20.Qf3??. A genuine 20.Rd3 lifts a rook
  // toward h3, with queen support, against a king castled on g8.
  auto result = analyze("r1b2rk1/ppp1np2/3qp3/1P2N1p1/P2PQ3/2P5/5PPP/R2R2K1 w - - 0 20", 4);
  auto it = std::find_if(result.root_moves.begin(), result.root_moves.end(),
      [](const auto& entry){return move_to_uci(entry.move)=="d1d3";});
  require(it != result.root_moves.end(), "tckaf 20.Rd3 legal");
  require(it->v4100_rook_lift_bonus > 0,
          "genuine rook-lift preparation receives bounded bonus");
  require(it->v4100_rook_lift_bonus <= 14, "rook lift bonus is small");
  std::cout << "ROOK_LIFT_BONUS=" << it->v4100_rook_lift_bonus << '\n';
}
void test_24_rook_lift_shape() {
  // tckafQbA 24.Rd3: rook d1-d3 threatens a later Rf3+ against Kf7.
  auto result = analyze("r1b2r2/ppp1nk2/4p3/1Pq1P1Q1/P7/2P5/5PPP/R2R2K1 w - - 1 24", 4);
  const auto entry = std::find_if(result.root_moves.begin(), result.root_moves.end(),
      [](const auto& row){return move_to_uci(row.move)=="d1d3";});
  require(entry != result.root_moves.end(), "tckaf 24.Rd3 legal");
  require(entry->v4100_rook_lift_bonus > 0,
          "uncastled exposed enemy king also admits rook lift preparation");
  std::cout << "ROOK_LIFT_24_BONUS=" << entry->v4100_rook_lift_bonus << '\n';
}
void test_no_false_rook_lift() {
  // Without a queen the preparation is not an attacking battery.
  auto result = analyze("4k3/8/8/8/8/8/8/3RK3 w - - 0 20", 3);
  for (const auto& entry : result.root_moves)
    require(entry.v4100_rook_lift_bonus == 0,
            "no speculative rook lift in queenless ending");
  std::cout << "QUEENLESS_ROOK_LIFT=0\n";
}
void test_book_identity_and_real_case() {
  std::vector<std::string> lines;
  UciEngine engine([&](const std::string& s){lines.push_back(s);});
  engine.send_command("uci");
  require(std::find(lines.begin(),lines.end(),
      "id name JJUGLE Chaos v4.10.0 Verified Attack Planning")!=lines.end(),
      "new version identity");
  engine.send_command("setoption name OwnBook value false");
  engine.send_command("setoption name EvalMode value CHAOS");
  engine.send_command("ucinewgame");
  engine.send_command("position fen 3qr1k1/1p1b1ppn/4p2p/p1rpQ3/5P2/2NB4/PPP3PP/2KR3R w - - 0 22");
  lines.clear();
  engine.send_command("go movetime 1800");
  bool found = false;
  std::string move;
  for (const auto& line : lines) {
    if (line.find("info string v4100_attack_planning ") != std::string::npos)
      found = true;
    if (line.rfind("bestmove ",0) == 0)
      move = line.substr(9,4);
  }
  require(found,"new UCI telemetry emitted");
  require(move != "e5h5","avoid documented unsupported queen sortie");
  require(move.size()==4,"engine returns legal-sized move");
  std::cout << "GAME_LOBX_22_BESTMOVE=" << move << '\n';
}
void test_endgame_control() {
  // LOBX9aul, 81...f5?? lost a drawn rook ending. This test does not
  // claim the ending is solved; it checks that the safe rook move is found.
  auto result = analyze("8/5pk1/RPK3p1/8/8/8/8/1r6 b - - 2 81",7);
  require(move_to_uci(result.best_move)=="b1c1",
          "retain drawn-endgame rook defense rather than losing pawn push");
  std::cout << "GAME_LOBX_81_BESTMOVE=" << move_to_uci(result.best_move) << '\n';
}
}
int main(){
  test_rook_lift_shape();
  test_24_rook_lift_shape();
  test_no_false_rook_lift();
  test_book_identity_and_real_case();
  test_endgame_control();
  std::cout << "V4100_ATTACK_PLANNING=PASS\n";
}
