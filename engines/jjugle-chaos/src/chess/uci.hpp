#pragma once

#include <optional>
#include <string>

#include "chess/board.hpp"

namespace hebichess {

std::optional<Move> parse_uci_move(const Board& board, const std::string& text);
std::string move_to_uci(const Move& move);

}  // namespace hebichess
