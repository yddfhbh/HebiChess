#pragma once

#include <vector>

#include "chess/board.hpp"
#include "chess/move.hpp"

namespace hebichess {

std::vector<Move> generate_pseudo_legal_moves(const Board& board);
std::vector<Move> generate_legal_moves(Board& board);

inline std::vector<Move> generate_legal_moves(const Board& board) {
  Board copy = board;
  return generate_legal_moves(copy);
}

}  // namespace hebichess
