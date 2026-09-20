#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "chess/board.hpp"
#include "chess/move.hpp"

namespace hebichess {

constexpr std::uint32_t kOpeningBookFormatVersion = 1;

// The book key is the engine Zobrist key with an EP component removed when
// no legal en-passant capture exists (the python-chess FEN convention).
ZobristKey opening_book_key(const Board& board);

struct OpeningBookCandidate {
  Move move{};
  std::uint32_t weight{0};
};

class OpeningBook {
 public:
  bool load(std::span<const std::uint8_t> bytes) noexcept;
  void clear() noexcept;

  bool available() const noexcept { return !positions_.empty(); }
  bool empty() const noexcept { return positions_.empty(); }
  std::uint16_t max_book_ply() const noexcept { return max_book_ply_; }
  std::uint16_t min_move_count() const noexcept { return min_move_count_; }

  std::vector<OpeningBookCandidate> lookup(const Board& board) const;
  std::optional<Move> choose_move(const Board& board,
                                  std::uint64_t random_value) const;

  static std::uint16_t pack_move(const Move& move) noexcept;
  static std::optional<Move> unpack_move(std::uint16_t packed) noexcept;

 private:
  struct PositionEntry {
    std::uint64_t key{0};
    std::uint32_t move_offset{0};
    std::uint16_t move_count{0};
  };
  struct MoveEntry {
    std::uint16_t packed_move{0};
    std::uint32_t weight{0};
  };

  std::vector<PositionEntry> positions_;
  std::vector<MoveEntry> moves_;
  std::uint16_t max_book_ply_{0};
  std::uint16_t min_move_count_{0};
};

}  // namespace hebichess
