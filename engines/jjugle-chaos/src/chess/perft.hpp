#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "chess/movegen.hpp"

namespace hebichess {

std::uint64_t perft(Board& board, int depth);
std::vector<std::pair<Move, std::uint64_t>> perft_divide(Board& board,
                                                          int depth);

}
