#pragma once

#include <vector>

#include "chess/board.hpp"
#include "chess/move.hpp"

namespace hebichess {

std::vector<Move> generate_pseudo_legal_moves(const Board& board);
// Captures, en passant, and promotions only.  This is intended for
// non-check quiescence nodes; callers still need legal-move filtering.
std::vector<Move> generate_pseudo_legal_tactical_moves(const Board& board);
std::vector<Move> generate_legal_moves(Board& board);
std::vector<Move> generate_legal_tactical_moves(Board& board);

inline std::vector<Move> generate_legal_moves(const Board& board) {
  Board copy = board;
  return generate_legal_moves(copy);
}

inline std::vector<Move> generate_legal_tactical_moves(const Board& board) {
  Board copy = board;
  return generate_legal_tactical_moves(copy);
}

}  // namespace hebichess
