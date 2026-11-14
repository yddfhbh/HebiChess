#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "chess/board.hpp"
#include "chess/eval.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"
#include "chess/uci_engine.hpp"

using namespace hebichess;
namespace {
constexpr const char* kWinning27 =
  "3b1rk1/1p1n1ppp/p3p1b1/1q1pP1B1/1P1P4/4NN1P/3Q1PP1/2R3K1 b - - 11 27";
constexpr const char* kHalloween12 =
  "r1b1kb1r/pp2nppp/2pq4/8/3P1P1n/2NBQ3/PPP3PP/R1B1K2R w KQkq - 4 12";
const char* kHalloween11Uci =
  "e2e4 e7e5 g1f3 g8f6 b1c3 b8c6 f3e5 c6e5 "
  "d2d4 e5g6 e4e5 f6g8 f2f4 d7d6 f1d3 c7c6 "
  "e5d6 d8d6 d1e2 g8e7";

void require(bool ok,const char* why){
 if(!ok){std::cerr<<"V4912_FAIL="<<why<<"\n";std::exit(1);}
}
struct Game {
 Board board=Board::initial();
 std::vector<Move> moves;
 std::vector<ZobristKey> keys{board.zobrist_key()};
 void replay(const std::string& line) {
   std::istringstream in(line);
   for(std::string uci;in>>uci;){
      const auto m=parse_uci_move(board,uci);
      require(m.has_value(),"fixture UCI move is legal");
      moves.push_back(*m);
      board.make_move(*m);
      keys.push_back(board.zobrist_key());
   }
 }
 SearchResult analyze(int depth,bool with_style) const {
    SearchLimits limits;
    limits.eval_mode=EvalMode::CHAOS;
    limits.max_depth=depth;
    limits.position_history_keys=keys;
    limits.recent_game_moves=moves;
    limits.use_root_style_selection=with_style;
    clear_transposition_table();clear_search_heuristics();
    return search(board,limits);
 }
};
void test_halloween_book_move_is_preserved() {
    Game game;
    game.replay("e2e4 e7e5 g1f3 g8f6 b1c3 b8c6");
    require(parse_uci_move(game.board,"f3e5").has_value(),
            "4.Nxe5 remains legal and no opening move is banned");
}
void test_queen_chain_is_verified_not_blindly_vetoed() {
    Game game;
    game.replay(kHalloween11Uci);
    const auto old=game.analyze(4,false);
    const auto decision=game.analyze(4,true);
    require(move_to_uci(old.best_move)=="e2e3","11 objective fixture expected Qe3");
    require(decision.v4912_development_candidates>0,"11 development candidates considered");
    require(decision.score==old.score,"queen chain preserves objective evaluation");
    // Intentional: no unsupported claim that a safe alternative has been proven.
    std::cout<<"V4912_11_OBJECTIVE="<<move_to_uci(old.best_move)
             <<" final="<<move_to_uci(decision.best_move)
             <<" candidates="<<decision.v4912_development_candidates<<"\n";
}
void test_halloween_12_castling_resource() {
    Game game;
    game.replay(std::string(kHalloween11Uci)+" e2e3 g6h4");
    require(game.board.to_fen()==kHalloween12,"12 FEN reconstructed");
    const auto objective=game.analyze(4,false);
    const auto final=game.analyze(4,true);
    require(move_to_uci(objective.best_move)=="e3f2","12 shallow baseline Qf2");
    require(move_to_uci(final.best_move)=="e1g1","12 bounded castling proof");
    require(final.score==objective.score,"12 no forged objective score");
    std::cout<<"V4912_12_CASTLING="<<move_to_uci(final.best_move)<<"\n";
}
void test_winning_conversion() {
    auto board=Board::from_fen(kWinning27);require(board.has_value(),"27 FEN valid");
    SearchLimits limits;limits.max_depth=4;limits.eval_mode=EvalMode::CHAOS;
    limits.use_root_style_selection=false;
    clear_transposition_table();clear_search_heuristics();
    auto objective=search(*board,limits);
    require(move_to_uci(objective.best_move)=="f7f6","27 baseline unsafe f6 reproduced");
    limits.use_root_style_selection=true;
    clear_transposition_table();clear_search_heuristics();
    auto final=search(*board,limits);
    require(move_to_uci(final.best_move)=="d8g5","27 Bxg5 safe bishop trade");
    require(final.v4912_conversion_selected,"27 conversion guard selected");
    require(final.score==objective.score,"27 preserve objective score");
    std::cout<<"V4912_27_SAFETY="<<move_to_uci(final.best_move)
             <<" candidates="<<final.v4912_conversion_candidates<<"\n";
}
void test_uci_and_hce() {
    std::vector<std::string> output;
    UciEngine uci([&](const std::string& line){output.push_back(line);});
    uci.send_command("uci");
    bool found=false;
    for(const auto& line:output)
       if(line=="id name JJUGLE Chaos v4.9.12 Development Continuation")found=true;
    require(found,"v4.9.12 UCI identity");
    auto b=Board::from_fen(kWinning27);require(b.has_value(),"HCE fixture");
    require(evaluate_hce(*b)==evaluate_breakdown(*b,b->side_to_move()).total,
            "ordinary HCE evaluation unchanged");
}
}
int main() {
    test_halloween_book_move_is_preserved();
    test_queen_chain_is_verified_not_blindly_vetoed();
    test_halloween_12_castling_resource();
    test_winning_conversion();
    test_uci_and_hce();
    std::cout<<"V4912_DEVELOPMENT_CONVERSION=PASS\n";
}
