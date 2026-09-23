#pragma once

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "chess/board.hpp"
#include "chess/move.hpp"

namespace hebichess {

// A legal side has at most 16 pieces including its king.  Any non-king piece
// can emit no more than 27 pseudo-legal destinations (a queen from the board
// centre); the king can emit at most ten (eight steps plus castling).  416 is
// therefore a conservative geometric upper bound for the pseudo list:
// 15 * 27 + 10.  It deliberately covers pseudo-legal lists too, not merely
// the chess-record 218 legal moves.
constexpr std::size_t kMaxPseudoLegalMoves = 416;

class FixedMoveList {
 public:
  void push_back(const Move& move) {
    if (count_ == moves_.size()) {
      assert(false && "FixedMoveList capacity exceeded");
      std::abort();
    }
    moves_[count_++] = move;
  }

  void clear() noexcept { count_ = 0; }
  void truncate(std::size_t count) noexcept {
    assert(count <= count_);
    count_ = count;
  }
  [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
  [[nodiscard]] std::size_t size() const noexcept { return count_; }
  Move& operator[](std::size_t index) noexcept { return moves_[index]; }
  const Move& operator[](std::size_t index) const noexcept { return moves_[index]; }
  Move* begin() noexcept { return moves_.data(); }
  const Move* begin() const noexcept { return moves_.data(); }
  Move* end() noexcept { return moves_.data() + count_; }
  const Move* end() const noexcept { return moves_.data() + count_; }

 private:
  std::array<Move, kMaxPseudoLegalMoves> moves_{};
  std::size_t count_{0};
};

struct MovegenAllocationProfile {
  std::uint64_t allocations{0};
  std::uint64_t reallocations{0};
  std::uint64_t allocated_bytes{0};
};

// The scope is profile-only instrumentation for the established vector path.
// It observes vector capacity growth without changing its allocation policy.
class ScopedMovegenAllocationProfile {
 public:
  explicit ScopedMovegenAllocationProfile(MovegenAllocationProfile* profile) noexcept;
  ~ScopedMovegenAllocationProfile();
  ScopedMovegenAllocationProfile(const ScopedMovegenAllocationProfile&) = delete;
  ScopedMovegenAllocationProfile& operator=(const ScopedMovegenAllocationProfile&) = delete;

 private:
  MovegenAllocationProfile* previous_{nullptr};
};

// QSearch can reuse this metadata after a legal-move filter.  It is kept
// separate from the ordinary move-generation API so main search continues to
// use its established path unchanged.
struct LegalMoveWithCheck {
  Move move{};
  bool gives_check{false};
};

std::vector<Move> generate_pseudo_legal_moves(const Board& board);
// Captures, en passant, and promotions only.  This is intended for
// non-check quiescence nodes; callers still need legal-move filtering.
std::vector<Move> generate_pseudo_legal_tactical_moves(const Board& board);
void generate_pseudo_legal_moves(const Board& board, FixedMoveList& moves);
void generate_pseudo_legal_tactical_moves(const Board& board, FixedMoveList& moves);
std::vector<Move> filter_legal_moves(Board& board, const std::vector<Move>& pseudo_moves);
void filter_legal_moves_in_place(Board& board, FixedMoveList& moves);
std::vector<Move> generate_legal_moves(Board& board);
std::vector<Move> generate_legal_tactical_moves(Board& board);
std::vector<LegalMoveWithCheck> generate_legal_moves_with_check(Board& board);
std::vector<LegalMoveWithCheck> generate_legal_tactical_moves_with_check(Board& board);

inline std::vector<Move> generate_legal_moves(const Board& board) {
  Board copy = board;
  return generate_legal_moves(copy);
}

inline std::vector<Move> generate_legal_tactical_moves(const Board& board) {
  Board copy = board;
  return generate_legal_tactical_moves(copy);
}

}  // namespace hebichess
