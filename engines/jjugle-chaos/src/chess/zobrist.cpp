#include "chess/zobrist.hpp"

#include <array>

namespace hebichess {
namespace {

constexpr std::uint64_t kSeed = 0x9e3779b97f4a7c15ULL;

constexpr std::uint64_t next_key(std::uint64_t value) noexcept {
  value += kSeed;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

struct Keys {
  std::array<std::array<std::array<ZobristKey, Square::kSquareCount>, 2>, 7> pieces{};
  std::array<ZobristKey, 16> castling{};
  std::array<ZobristKey, Square::kSquareCount> en_passant{};
  ZobristKey side{0};
};

const Keys& keys() noexcept {
  static const Keys result = [] {
    Keys keys;
    std::uint64_t value = kSeed;
    for (auto& by_type : keys.pieces)
      for (auto& by_color : by_type)
        for (auto& key : by_color) key = next_key(value++);
    for (auto& key : keys.castling) key = next_key(value++);
    for (auto& key : keys.en_passant) key = next_key(value++);
    keys.side = next_key(value++);
    return keys;
  }();
  return result;
}

std::size_t rights_index(CastlingRights rights) noexcept {
  return static_cast<std::size_t>(rights.white_king_side) |
         (static_cast<std::size_t>(rights.white_queen_side) << 1) |
         (static_cast<std::size_t>(rights.black_king_side) << 2) |
         (static_cast<std::size_t>(rights.black_queen_side) << 3);
}

}  // namespace

ZobristKey piece_zobrist(Piece piece, Square square) noexcept {
  if (piece.is_empty() || !square.is_valid()) return 0;
  return keys().pieces[static_cast<std::size_t>(piece.type)]
                        [static_cast<std::size_t>(piece.color)][square.index()];
}

ZobristKey side_zobrist() noexcept { return keys().side; }

ZobristKey castling_zobrist(CastlingRights rights) noexcept {
  return keys().castling[rights_index(rights)];
}

ZobristKey en_passant_zobrist(Square square) noexcept {
  return square.is_valid() ? keys().en_passant[square.index()] : 0;
}

ZobristKey compute_zobrist(const Board& board) noexcept {
  ZobristKey key = side_zobrist() * static_cast<std::size_t>(board.side_to_move() == Color::Black);
  key ^= castling_zobrist(board.castling_rights());
  key ^= en_passant_zobrist(board.en_passant_target());
  for (std::uint8_t index = 0; index < Square::kSquareCount; ++index) {
    const Square square = Square::from_index(index);
    key ^= piece_zobrist(board.piece_at(square), square);
  }
  return key;
}

}  // namespace hebichess
