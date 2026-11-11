#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include "chess/board.hpp"
#include "chess/movegen.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"
using namespace hebichess;

int main() {
  // Lichess FmmHquk4 after 20...Nb4, general board/history driven test.
  // The implementation is not allowed to branch on this FEN or move.
  const std::vector<std::string> game = {
      "e2e4", "c7c5", "g1f3", "b8c6", "d2d4", "c5d4",
      "c2c3", "g8f6", "e4e5", "f6d5", "f1c4", "e7e6",
      "c4d5", "e6d5", "c3d4", "d7d6", "e1g1", "f8e7",
      "f1e1", "e8g8", "b1c3", "d6e5", "d4e5", "c8e6",
      "h2h4", "h7h6", "d1d3", "c6b4", "d3d2", "e6f5",
      "d2f4", "f5g6", "e1d1", "b4c2", "a1b1", "c2b4",
      "b1a1", "b4c2", "a1b1", "c2b4"
  };
  Board board=Board::initial();
  std::vector<ZobristKey> history{board.zobrist_key()};
  for(const std::string& u : game) {
    const auto move = parse_uci_move(board,u);
    if(!move) {std::cerr<<"ILLEGAL_MOVE "<<u<<" FEN="<<board.to_fen()<<"\n";return 2;}
    board.make_move(*move);
    history.push_back(board.zobrist_key());
  }
  std::cout << "FEN=" << board.to_fen() << "\n";
  auto repeat = parse_uci_move(board,"b1a1");
  if (!repeat) {std::cerr<<"REPEAT_MOVE_ILLEGAL\n";return 3;}
  Board after=board; after.make_move(*repeat);
  const int count = static_cast<int>(std::count(history.begin(),history.end(),after.zobrist_key()));
  std::cout << "PRIOR_REPETITIONS=" << count << "\n";
  if(count!=2) return 4;
  SearchLimits limits;
  limits.eval_mode=EvalMode::CHAOS;
  limits.max_depth=4;
  limits.position_history_keys=history;
  clear_transposition_table();clear_search_heuristics();
  const auto out=search(board,limits);
  std::cout << "BEST=" << move_to_uci(out.best_move) << " SCORE=" << out.score
    << " FIGHT=" << out.v496_fighting_spirit << " CAP=" << out.v496_loss_cap_cp << "\n";
  if (out.best_move == *repeat) {
    std::cerr << "REPEATED_DRAW_IN_COMPLEX_MIDDLEGAME\n";
    return 5;
  }
  Board next=board; next.make_move(out.best_move);
  if(std::count(history.begin(),history.end(),next.zobrist_key())>=2) return 6;
  std::cout << "V496_FIGHTING_SPIRIT_REAL_GAME=PASS\n";
  return 0;
}
