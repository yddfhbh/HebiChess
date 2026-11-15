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
void require(bool ok,const char* message){
  if(!ok){std::cerr<<"V4915_FAILURE="<<message<<'\n';std::exit(1);}
}
struct Case {const char* label; const char* fen;const char* cmd;const char* avoid; const char* require_move;};
void run(const Case& c){
  auto board=Board::from_fen(c.fen);
  require(bool(board),"real PGN FEN parses");
  std::vector<std::string> output;
  UciEngine engine([&](const std::string& message){output.push_back(message);});
  engine.send_command("setoption name EvalMode value CHAOS");
  // Each fixture is a separate game; the global TT and search heuristics
  // must not carry over from the preceding position.
  engine.send_command("ucinewgame");
  engine.send_command(std::string("position fen ")+c.fen);
  engine.send_command(c.cmd);
  std::string result;
  bool diagnostics=false;
  bool time_record=false;
  for(const auto& line:output){
    if(line.rfind("bestmove ",0)==0)result=line.substr(9,4);
    if(line.find("info string v4915_attack_formation ")!=std::string::npos)diagnostics=true;
    if(line.find("info string tm soft ")!=std::string::npos)time_record=true;
  }
  require(result.size()==4,"search returns a move");
  require(diagnostics && time_record,"full diagnostic telemetry");
  require(bool(parse_uci_move(*board,result)),"search returns legal move");
  if(c.avoid && *c.avoid) require(result!=c.avoid,"prevent documented tactical error");
  if(c.require_move && *c.require_move)require(result==c.require_move,"retain concrete sacrifice");
  std::cout<<"CASE="<<c.label<<" BESTMOVE="<<result<<" PASS\n";
}
}
int main(){
  // DE9Q4KMj: avoid 12...Qe5's unsupported upper-bound queen adventure.
  run({"DE9Q4KMj_12_queen", "r1b2rk1/pp2ppbp/2np2p1/3N3q/2P3nP/6P1/PPN1PPB1/1RBQ1RK1 b - - 2 12", "go movetime 2300", "h5e5", ""});
  // 6CGQ7z5B: do not repeat 35.Bd4 if a safe 35.g4 queen tempo is verified.
  run({"6CGQ7z5B_35_pawn_tempo", "r4bk1/5pp1/1Bp4p/p2rP2q/P4Q2/1R2K3/2P3PP/1R6 w - - 7 35", "go depth 9 movetime 10000", "b6d4", "g2g4"});
  // ZHbRP3f8: preserve the speculative but concrete sacrifice combination.
  run({"ZHbRP3f8_31_minor_sacrifice", "r3k3/P3p3/2pp3b/4nqp1/4N2r/1P2BPQ1/2P3K1/R3R3 b - - 1 31", "go depth 7", "", "e5f3"});
  run({"ZHbRP3f8_33_rook_sacrifice", "r3k3/P7/2pp3b/5qp1/7r/1P2BQ2/2P3K1/R3R3 b - - 0 33", "go depth 7", "", "h4h2"});
  std::cout<<"V4915=PASS\n";
}
