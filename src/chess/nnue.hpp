#pragma once

#include <optional>
#include <string>

#include "chess/board.hpp"

namespace hebichess {

bool load_nnue_network(const std::string& path, std::string& error);
bool nnue_network_available() noexcept;
// Float diagnostic used by parity tooling. Search uses the rounded int API.
std::optional<float> evaluate_nnue_network_raw(const Board& board) noexcept;
std::optional<int> evaluate_nnue_network(const Board& board) noexcept;
void clear_nnue_network() noexcept;

}  // namespace hebichess
