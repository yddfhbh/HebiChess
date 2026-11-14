#include <cstdlib>
#include <iostream>
#include <string>
#include "chess/board.hpp"
#include "chess/eval.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"
#include "chess/uci_engine.hpp"
using namespace hebichess;
namespace {
constexpr const char* k31 = "3rq1k1/p2r4/1pQp4/1P1P1p2/R3p1p1/6R1/1PP2PP1/6K1 w - - 0 31";
constexpr const char* k35 = "3r2k1/p6r/1pQp4/1P1P1p2/R3p1p1/R7/1PP1KPq1/8 w - - 0 35";
void need(bool good,const std::string& msg) {
 if(!good) {std::cerr << "V4911_FAIL=" << msg << "\n";std::exit(1);}
}
SearchResult check(const char* fen,int depth,bool style) {
 auto board=Board::from_fen(fen);need(board.has_value(),"valid FEN");
 SearchLimits limits; limits.max_depth=depth;limits.eval_mode=EvalMode::CHAOS;
 limits.use_root_style_selection=style;
 clear_transposition_table();clear_search_heuristics();
 return search(*board,limits);
}
void check_31() {
 auto r=check(k31,9,false);
 std::cout << "V4911_31_OBJECTIVE=" << move_to_uci(r.best_move) << " depth=" << r.completed_depth << "\n";
 need(r.completed_depth==9,"31 objective depth 9");
 need(move_to_uci(r.best_move)=="c6c3","31 objective Qc3 instead of Rc3");
 auto styled=check(k31,9,true);
 need(move_to_uci(styled.best_move)=="c6c3","31 final Qc3");
}
void check_35() {
 auto objective=check(k35,9,false);
 std::cout << "V4911_35_OBJECTIVE=" << move_to_uci(objective.best_move) << " depth=" << objective.completed_depth << "\n";
 need(objective.completed_depth==9,"35 objective depth 9");
 auto result=check(k35,9,true);
 std::cout << "V4911_35_DEFENSE=" << move_to_uci(result.best_move) << " depth=" << result.completed_depth << " candidates=" << result.v4911_queen_defense_candidates << "\n";
 need(result.completed_depth==9,"35 final depth 9");
 need(move_to_uci(result.best_move)=="a3g3","35 queen-challenging Rg3 instead of Rxa7");
 need(result.v4911_queen_defense_selected,"35 queen-challenge bounded proof selected");
 need(result.score==objective.score,"35 objective score not falsified by root guard");
}
void check_hce_consistency() {
 auto b=Board::from_fen(k35);need(b.has_value(),"HCE board");
 const int legacy=evaluate_breakdown(*b,b->side_to_move()).total;
 need(evaluate_hce(*b)==legacy,"HCE score unchanged");
}
void check_uci() {
 std::vector<std::string> lines;UciEngine engine([&](const std::string& s){lines.push_back(s);});
 engine.send_command("uci");bool ok=false;
 for(const auto& s: lines)ok|=s=="id name JJUGLE Chaos v4.9.12 Development Continuation";
 need(ok,"UCI identifies v4.9.11");
}
}
int main() {
 check_31();check_35();check_hce_consistency();check_uci();
 std::cout << "V4911_DEFENSIVE_PRESSURE=PASS\n";
}
