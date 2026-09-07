#pragma once

#include "chess/board.hpp"
#include "chess/move.hpp"

namespace hebichess {

// Static exchange evaluation in centipawns from the moving side's perspective.
int static_exchange_eval(const Board& board, const Move& move) noexcept;
bool see_ge(const Board& board, const Move& move, int threshold) noexcept;

}  // namespace hebichess
