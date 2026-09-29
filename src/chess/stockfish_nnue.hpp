#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "chess/board.hpp"

namespace hebichess {

struct StockfishNnueStats {
  std::uint64_t evaluation_calls{0};
  std::uint64_t accumulator_transform_requests{0};
  std::uint64_t failures{0};
};

bool load_stockfish_nnue_network(const std::string& path, std::string& error);
bool load_stockfish_nnue_network_bytes(const std::uint8_t* bytes, std::size_t size,
                                       std::string& error);
bool stockfish_nnue_network_available() noexcept;
void clear_stockfish_nnue_network() noexcept;
std::size_t stockfish_nnue_network_storage_bytes() noexcept;
std::optional<int> evaluate_stockfish_nnue(const Board& board) noexcept;
StockfishNnueStats stockfish_nnue_stats() noexcept;
void reset_stockfish_nnue_stats() noexcept;

// Test-only diagnostic: exercise upstream do_move/undo_move and accumulator
// incremental updates, comparing both perspectives with a fresh refresh.
bool stockfish_nnue_incremental_parity(const Board& board, const Move& move,
                                       std::string& error) noexcept;

}  // namespace hebichess
