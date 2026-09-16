#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "chess/board.hpp"

namespace hebichess {

bool load_nnue_network(const std::string& path, std::string& error);
// Uses the same strict v1/v2/v3 parser as the file loader.  The browser
// bridge supplies an ArrayBuffer through WASM linear memory with this entry.
bool load_nnue_network_bytes(const std::uint8_t* bytes, std::size_t size, std::string& error);
bool nnue_network_available() noexcept;
// Float diagnostic used by parity tooling. Search uses the rounded int API.
std::optional<float> evaluate_nnue_network_raw(const Board& board) noexcept;
std::optional<int> evaluate_nnue_network(const Board& board) noexcept;
void clear_nnue_network() noexcept;

}  // namespace hebichess
