#include "chess/perft.hpp"

namespace hebichess {

std::uint64_t perft(Board& board, int depth) {
  if (depth <= 0) return 1;
  std::uint64_t nodes = 0;
  for (const Move& move : generate_legal_moves(board)) {
    const UndoState undo = board.make_move(move);
    nodes += perft(board, depth - 1);
    board.unmake_move(move, undo);
  }
  return nodes;
}

std::vector<std::pair<Move, std::uint64_t>> perft_divide(Board& board,
                                                          int depth) {
  std::vector<std::pair<Move, std::uint64_t>> result;
  if (depth <= 0) return result;
  for (const Move& move : generate_legal_moves(board)) {
    const UndoState undo = board.make_move(move);
    result.emplace_back(move, perft(board, depth - 1));
    board.unmake_move(move, undo);
  }
  return result;
}

}
