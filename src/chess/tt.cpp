#include "chess/tt.hpp"

#include <algorithm>

namespace hebichess {

TranspositionTable::TranspositionTable(std::size_t megabytes) {
  const std::size_t requested = std::max<std::size_t>(1, (megabytes * 1024 * 1024) / sizeof(TTEntry));
  std::size_t count = 1;
  while (count <= requested / 2) count <<= 1;
  entries_.resize(count);
}

void TranspositionTable::clear() noexcept {
  for (TTEntry& entry : entries_) entry = {};
}

const TTEntry* TranspositionTable::probe(ZobristKey key) const noexcept {
  const TTEntry& entry = entries_[key & (entries_.size() - 1)];
  return entry.occupied && entry.key == key ? &entry : nullptr;
}

void TranspositionTable::store(ZobristKey key, int depth, int score,
                               TTBound bound, std::optional<Move> best_move) noexcept {
  TTEntry& entry = entries_[key & (entries_.size() - 1)];
  if (entry.occupied && entry.key != key && entry.depth > depth) return;
  entry = {key, depth, score, bound, best_move, true};
}

std::size_t TranspositionTable::hashfull() const noexcept {
  const std::size_t sample = std::min<std::size_t>(1000, entries_.size());
  std::size_t used = 0;
  for (std::size_t i = 0; i < sample; ++i) used += entries_[i].occupied;
  return sample == 0 ? 0 : used * 1000 / sample;
}

}  // namespace hebichess
