#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include "chess/board.hpp"
#include "chess/movegen.hpp"
#include "chess/opening_book.hpp"
#include "chess/search.hpp"
#include "chess/time_management.hpp"
#include "chess/uci.hpp"
#include "chess/uci_engine.hpp"

using namespace hebichess;

namespace {
void require(bool b, const char* msg) {
  if (!b) {std::cerr << "V4914_FAILURE=" << msg << '\n';std::exit(1);}
}

void w16(std::vector<std::uint8_t>& d, std::uint16_t n) {
  d.push_back(n & 255); d.push_back((n >> 8) & 255);
}
void w32(std::vector<std::uint8_t>& d, std::uint32_t n) {
  for (int i = 0; i < 4; ++i) d.push_back((n >> (8*i)) & 255);
}
void w64(std::vector<std::uint8_t>& d, std::uint64_t n) {
  for (int i = 0; i < 8; ++i) d.push_back((n >> (8*i)) & 255);
}

struct BookCase { ZobristKey key; std::vector<std::pair<Move,std::uint32_t>> moves; };
Board after(const std::vector<const char*>& seq) {
  Board b = Board::initial();
  for (const auto* s : seq) {
    const auto m = parse_uci_move(b,s);
    require(m.has_value(),"legal fixture moves");
    b.make_move(*m);
  }
  return b;
}
Move valid(Board b, const char* uci) {
  auto m = parse_uci_move(b, uci);
  require(m.has_value(), "legal book fixture");
  return *m;
}

std::vector<std::uint8_t> book_bytes() {
  const Board h3 = after({"h2h3"});
  const Board a3 = after({"a2a3"});
  const Board e4 = after({"e2e4"});
  const Board halloween = after({"e2e4","e7e5","g1f3","b8c6",
                                 "b1c3","g8f6"});
  std::vector<BookCase> entries{
    {opening_book_key(h3), {{valid(h3,"h7h5"),100}}},
    {opening_book_key(a3), {{valid(a3,"h7h5"),90}, {valid(a3,"e7e5"),10}}},
    {opening_book_key(e4), {{valid(e4,"h7h5"),100}}},
    {opening_book_key(halloween), {{valid(halloween,"f3e5"),100}}},
  };
  std::sort(entries.begin(),entries.end(),[](const auto& x,const auto& y){return x.key<y.key;});
  std::vector<std::uint8_t> d;
  for (auto c : std::string("HEBIBOOK")) d.push_back(static_cast<std::uint8_t>(c));
  w32(d,kOpeningBookFormatVersion); w32(d,entries.size());
  std::uint32_t total=0;
  for(const auto& e: entries) total += static_cast<std::uint32_t>(e.moves.size());
  w32(d,total); w16(d,16);w16(d,0);
  std::uint32_t offset=0;
  for (const auto& e : entries) {
    w64(d,e.key);w32(d,offset);w16(d,e.moves.size());w16(d,0);
    offset+=static_cast<std::uint32_t>(e.moves.size());
  }
  for (const auto& e : entries) for (const auto& [m,w] : e.moves) {
    w16(d,OpeningBook::pack_move(m));w16(d,0);w32(d,w);
  }
  return d;
}

bool has(const std::vector<std::string>& lines, const std::string& needle) {
  return std::any_of(lines.begin(),lines.end(),[&](const std::string& s){return s.find(needle)!=std::string::npos;});
}
std::string latest_best(const std::vector<std::string>& lines) {
  for (auto i=lines.rbegin();i!=lines.rend();++i)
    if (i->rfind("bestmove ",0)==0) return *i;
  return "";
}

void check_book_filter() {
  std::vector<std::string> out;
  UciEngine e([&](const std::string& s){out.push_back(s);});
  const auto book=book_bytes();
  require(e.load_opening_book_bytes(book),"synthetic book parses");
  e.send_command("setoption name OwnBook value true");
  e.send_command("position startpos moves h2h3");
  out.clear(); e.send_command("go depth 2");
  require(has(out,"v4914_book_filter h5_flank_reply engine_search"),
          "h3 h5 book fallback to actual search");
  require(!has(out,"info string book hit"),"h3 exception did not use book");
  require(latest_best(out)!="bestmove h7h5","avoid first h3 h5");
  require(has(out,"v4914_decision objective"),"search emits decision telemetry");

  e.send_command("position startpos moves a2a3");
  out.clear();e.send_command("go depth 2");
  require(latest_best(out)=="bestmove e7e5","a3 retains alternative repertoire book move");
  require(has(out,"info string book hit"),"a3 alternative from book, no search");

  e.send_command("position startpos moves e2e4");
  out.clear();e.send_command("go depth 2");
  require(latest_best(out)=="bestmove h7h5","other positions unmodified");
  require(has(out,"info string book hit"),"ordinary h5 book move allowed");

  e.send_command("position startpos moves e2e4 e7e5 g1f3 b8c6 b1c3 g8f6");
  out.clear();e.send_command("go depth 2");
  require(latest_best(out)=="bestmove f3e5","Halloween 4.Nxe5 stays in book");
  require(has(out,"info string book hit"),"Halloween book retained");
  std::cout<<"BOOK_FILTER_RETAIN_REPERTOIRE=PASS\n";
}

void check_time_budget() {
  const auto previous=scale_time_budget({3017,12068},75);
  const auto faster=scale_time_budget(allocate_time_budget(180000,2000,false,4),75);
  require(previous.hard_ms>8500,"former hard about nine seconds");
  require(faster.hard_ms>=3500 && faster.hard_ms<=5000,
          "early 180+2 hard about four to five seconds");
  require(faster.hard_ms < previous.hard_ms && faster.soft_ms>0,
          "early hard shorter while soft is positive");
  const auto middle=allocate_time_budget(99000,1000,false,27);
  require(middle.hard_ms>=middle.soft_ms,"middle time unchanged");
  const auto emergency=allocate_time_budget(1700,0,false,40);
  require(emergency.hard_ms<1700,"low clock protected");
  std::cout<<"TIME_180_2=SOFT_"<<faster.soft_ms<<"_HARD_"<<faster.hard_ms<<"\n";
}

void check_choice() {
  const Board b=after({"h2h3","h7h5","e2e4","d7d5","e4d5","d8d5","b1c3"});
  SearchLimits lim;
  lim.max_depth=4;
  lim.eval_mode=EvalMode::CHAOS;
  lim.use_root_style_selection=true;
  clear_transposition_table();clear_search_heuristics();
  const auto result=search(b,lim);
  require(result.completed_depth==4,"completed search depth");
  require(result.objective_best_move.from.is_valid(),"objective root move tracked");
  require(result.best_move.from.is_valid(),"chosen move tracked");
  require(std::find_if(result.root_moves.begin(),result.root_moves.end(),
          [&](const RootMoveInfo& r){return r.move==result.objective_best_move;})!=result.root_moves.end(),
          "objective entry in complete root list");
  std::vector<std::string> output;
  UciEngine e([&](const std::string& s){output.push_back(s);});
  e.send_command("position startpos moves h2h3 h7h5 e2e4 d7d5 e4d5 d8d5 b1c3");
  e.send_command("go depth 4");
  require(has(output,"v4914_decision objective "),"UCI emits objective and chosen");
  require(has(output,"info string tm soft "),"UCI emits timing profile");
  require(has(output,"info string qprofile "),"UCI emits qsearch profile");
  std::cout<<"DECISION_PROFILE=PASS chosen="<<move_to_uci(result.best_move)
           <<" objective="<<move_to_uci(result.objective_best_move)<<"\n";
}
} // namespace

int main(){check_book_filter();check_time_budget();check_choice();std::cout<<"V4914=PASS\n";}
