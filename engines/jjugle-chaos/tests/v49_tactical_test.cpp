#include <algorithm>
#include <iostream>
#include "chess/search.hpp"
#include "chess/movegen.hpp"
#include "chess/uci.hpp"
using namespace hebichess;
int main(){
  // BlMQafQ1, after 14...Kd8: 15.Nxa8 is the exchange win.
  const auto p=Board::from_fen("r2k1b1r/1bN3pp/pq1p1n2/1p1P4/5P2/8/PPP1Q1PP/R1B1K2R w KQ - 5 15");
  if(!p){std::cerr<<"FEN_BAD\n";return 1;}
  clear_transposition_table();clear_search_heuristics();
  SearchLimits limits; limits.max_depth=6; limits.eval_mode=EvalMode::CHAOS;
  const auto result=search(*p,limits);
  const auto move=parse_uci_move(*p,"c7a8");
  if(!move || !(result.best_move==*move) || !result.v49_tactical_cashout){
    std::cerr<<"V49_ROOK_CAPTURE_NOT_FOUND selected="<<move_to_uci(result.best_move)
             <<" cashout="<<result.v49_tactical_cashout<<"\n";
    return 1;
  }
  clear_transposition_table();clear_search_heuristics();
  limits.eval_mode=EvalMode::HCE;
  if(search(*p,limits).v49_tactical_cashout){
    std::cerr<<"HCE_PATH_MODIFIED\n";return 1;
  }
  std::cout<<"V49_TACTICAL_CASHOUT_TEST=PASS move="<<move_to_uci(result.best_move)<<"\n";
}
