#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include "chess/board.hpp"
#include "chess/movegen.hpp"
using namespace hebichess;

bool equal_scan(const Board& b) {
  for (auto color : {Color::White, Color::Black}) {
    Square observed{};
    for (unsigned index = 0; index < 64; ++index) {
      Square sq = Square::from_index(index);
      if (b.piece_at(sq) == Piece{PieceType::King, color}) {observed = sq; break;}
    }
    if (b.find_king(color) != observed) return false;
  }
  return true;
}

bool test_moves(Board board, unsigned steps) {
  if (!equal_scan(board)) return false;
  const std::string initial = board.to_fen();
  const auto initial_key = board.zobrist_key();
  struct Record {Move m; UndoState u;};
  std::vector<Record> stack;
  std::uint32_t seed = 0x674e5ca9;
  for(unsigned turn=0;turn<steps;++turn) {
    auto moves = generate_legal_moves(board);
    if (moves.empty()) break;
    seed = seed * 1664525U + 1013904223U;
    const Move move=moves[seed % moves.size()];
    UndoState state=board.make_move(move);
    stack.push_back({move,state});
    if(!equal_scan(board)) {std::cerr << "king drift after move " << turn << '\n';return false;}
    if(turn%9==0) {
      const auto fen=board.to_fen();
      const auto key=board.zobrist_key();
      auto undo=board.make_null_move();
      if(!equal_scan(board))return false;
      board.unmake_null_move(undo);
      if(!equal_scan(board)||board.to_fen()!=fen||board.zobrist_key()!=key)return false;
    }
  }
  while(!stack.empty()) {
    const auto step=stack.back();stack.pop_back();
    board.unmake_move(step.m,step.u);
    if(!equal_scan(board)) {std::cerr<<"king drift after undo\n";return false;}
  }
  return board.to_fen()==initial && board.zobrist_key()==initial_key;
}
int main() {
  if (!test_moves(Board::initial(),120)) return 1;
  for (const auto* fen : {
    "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
    "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 17",
    "4k3/P7/8/8/8/8/7p/4K3 w - - 0 20",
    "8/8/8/8/8/8/8/8 w - - 0 1"}) {
    const auto parsed=Board::from_fen(fen);
    if (!parsed || !test_moves(*parsed,40)) {
      std::cerr<<"FEN failure: "<<fen<<'\n';return 2;
    }
  }
  std::cout<<"V495_KING_CACHE=PASS\n";
}
