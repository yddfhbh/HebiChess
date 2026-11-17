#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include "chess/uci_engine.hpp"
#include "chess/search.hpp"
#include "chess/board.hpp"
#include "chess/uci.hpp"
using namespace hebichess;
namespace {
void check(bool good,const char* message) {
  if (!good) { std::cerr << "V4101_FAILURE=" << message << '\n'; std::exit(1); }
}
const std::string kBefore14 =
    "e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 c2c3 d4c3 f1c4 c3b2 "
    "c1b2 e7e6 e1g1 g8f6 e4e5 f6e4 b1d2 e4c5 a1c1 f8e7 "
    "d1c2 e8g8 d2e4 c6e5 b2e5 c5e4";
const std::string kAfterSacrifice = kBefore14 +
    " e5g7 g8g7 c2e4 d7d5";
std::string play(const std::string& moves, int ms) {
  std::vector<std::string> lines;
  UciEngine engine([&](const std::string& line){lines.push_back(line);});
  engine.send_command("setoption name OwnBook value false");
  engine.send_command("setoption name EvalMode value CHAOS");
  engine.send_command("ucinewgame");
  engine.send_command("position startpos moves " + moves);
  engine.send_command("go movetime " + std::to_string(ms));
  std::string best;
  for (const auto& line: lines) if (line.rfind("bestmove ",0)==0)
    best=line.substr(9,4);
  check(!best.empty(),"search returned move");
  return best;
}
}
int main() {
  const auto sacrifice=play(kBefore14,1500);
  std::cout<<"SACRIFICE_14="<<sacrifice<<'\n';
  check(sacrifice=="e5g7", "14.Bxg7 attacking sacrifice preserved");
  for (int ms: {1000, 2500, 5000}) {
    const auto continuation=play(kAfterSacrifice,ms);
    std::cout<<"CONTINUATION_16_MS="<<ms<<" BESTMOVE="<<continuation<<'\n';
    check(continuation=="e4g4", "16.Qg4+ attacking continuation replaces Qe5+");
  }
  std::cout<<"V4101_ATTACK_CONTINUATION=PASS\n";
}
