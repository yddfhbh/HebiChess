#pragma once

#include "chess/types.hpp"

namespace hebichess {

enum class MoveFlag : std::uint8_t {
  Normal,
  Capture,
  DoublePawnPush,
  EnPassant,
  CastleKingSide,
  CastleQueenSide,
  Promotion,
  PromotionCapture,
};

struct Move {
  Square from{};
  Square to{};
  PieceType promotion{PieceType::None};
  MoveFlag flag{MoveFlag::Normal};

  constexpr bool is_promotion() const noexcept {
    return flag == MoveFlag::Promotion || flag == MoveFlag::PromotionCapture;
  }

  constexpr bool operator==(const Move&) const noexcept = default;
};

}  // namespace hebichess
