#pragma once

#include <cstdint>

namespace hebichess {

enum class Color : std::uint8_t {
  White,
  Black,
};

constexpr Color opposite(Color color) noexcept {
  return color == Color::White ? Color::Black : Color::White;
}

enum class PieceType : std::uint8_t {
  None,
  Pawn,
  Knight,
  Bishop,
  Rook,
  Queen,
  King,
};

struct Piece {
  PieceType type{PieceType::None};
  Color color{Color::White};

  constexpr bool is_empty() const noexcept {
    return type == PieceType::None;
  }

  constexpr bool is_colored() const noexcept {
    return !is_empty();
  }

  constexpr bool operator==(const Piece&) const noexcept = default;
};

class Square {
 public:
  constexpr Square() noexcept = default;

  static constexpr Square from_index(std::uint8_t index) noexcept {
    return Square(index < kSquareCount ? index : kInvalidIndex);
  }

  static constexpr Square from_file_rank(std::uint8_t file,
                                         std::uint8_t rank) noexcept {
    return file < kBoardSide && rank < kBoardSide
               ? Square(static_cast<std::uint8_t>(rank * kBoardSide + file))
               : Square(kInvalidIndex);
  }

  constexpr bool is_valid() const noexcept { return index_ < kSquareCount; }
  constexpr std::uint8_t index() const noexcept { return index_; }
  constexpr std::uint8_t file() const noexcept {
    return static_cast<std::uint8_t>(index_ % kBoardSide);
  }
  constexpr std::uint8_t rank() const noexcept {
    return static_cast<std::uint8_t>(index_ / kBoardSide);
  }

  constexpr bool operator==(const Square&) const noexcept = default;

  static constexpr std::uint8_t kBoardSide = 8;
  static constexpr std::uint8_t kSquareCount = kBoardSide * kBoardSide;
  static constexpr std::uint8_t kInvalidIndex = kSquareCount;

 private:
  explicit constexpr Square(std::uint8_t index) noexcept : index_(index) {}

  std::uint8_t index_{kInvalidIndex};
};

}  // namespace hebichess
