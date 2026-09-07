#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "chess/board.hpp"
#include "chess/move.hpp"

namespace hebichess {

enum class TTBound : std::uint8_t { Exact, Lower, Upper };

struct TTEntry {
  ZobristKey key{0};
  int depth{-1};
  int score{0};
  TTBound bound{TTBound::Exact};
  std::optional<Move> best_move{};
  bool occupied{false};
};

class TranspositionTable {
 public:
  explicit TranspositionTable(std::size_t megabytes = 64);

  void clear() noexcept;
  const TTEntry* probe(ZobristKey key) const noexcept;
  void store(ZobristKey key, int depth, int score, TTBound bound,
             std::optional<Move> best_move) noexcept;
  std::size_t hashfull() const noexcept;
  std::size_t size() const noexcept { return entries_.size(); }

 private:
  std::vector<TTEntry> entries_;
};

}  // namespace hebichess
