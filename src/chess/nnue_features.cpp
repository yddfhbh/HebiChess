#include "chess/nnue_features.hpp"

namespace hebichess {
namespace {

std::uint8_t orient(Square square, Color perspective) noexcept {
  // Black sees its home rank as rank 1; files are deliberately not mirrored.
  return perspective == Color::White ? square.index()
                                    : static_cast<std::uint8_t>(square.index() ^ 56U);
}

}  // namespace

NnueFeatures extract_nnue_features(const Board& board, Color perspective) noexcept {
  NnueFeatures result;
  const Square king = board.find_king(perspective);
  if (!king.is_valid()) return result;
  const std::uint32_t king_square = orient(king, perspective);
  for (std::uint8_t raw = 0; raw < Square::kSquareCount; ++raw) {
    const Square square = Square::from_index(raw);
    const Piece piece = board.piece_at(square);
    if (piece.is_empty()) continue;
    // Own color is 0, opponent color is 1. Piece types are Pawn=0..King=5.
    const std::uint32_t color = piece.color == perspective ? 0U : 1U;
    const std::uint32_t type = static_cast<std::uint32_t>(piece.type) - 1U;
    const std::uint32_t colored_type = color * 6U + type;
    result.indices[result.size++] = ((king_square * 12U + colored_type) * 64U) +
                                    orient(square, perspective);
  }
  return result;
}

}  // namespace hebichess
