#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "chess/board.hpp"

namespace hebichess {

struct Stockfish14NnueStats {
  std::uint64_t evaluation_calls{0};
  std::uint64_t accumulator_updates{0};
  std::uint64_t failures{0};
};

bool load_stockfish14_nnue_network(const std::string& path, std::string& error);
bool load_stockfish14_nnue_network_bytes(const std::uint8_t* bytes,
                                         std::size_t size, std::string& error);
bool stockfish14_nnue_network_available() noexcept;
void clear_stockfish14_nnue_network() noexcept;
std::size_t stockfish14_nnue_network_storage_bytes() noexcept;
std::optional<int> evaluate_stockfish14_nnue(const Board& board) noexcept;
Stockfish14NnueStats stockfish14_nnue_stats() noexcept;
void reset_stockfish14_nnue_stats() noexcept;
bool stockfish14_nnue_incremental_parity(const Board& board, const Move& move,
                                         std::string& error) noexcept;

}  // namespace hebichess
