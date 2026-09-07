#pragma once

#include "chess/board.hpp"

namespace hebichess {

constexpr int AGGRESSION_TOLERANCE_CP = 35;

int piece_value(PieceType type) noexcept;
int evaluate_material(const Board& board, Color perspective) noexcept;
int evaluate_piece_activity(const Board& board, Color perspective) noexcept;
int evaluate_mobility(const Board& board, Color perspective) noexcept;
int evaluate_king_safety(const Board& board, Color perspective) noexcept;
int evaluate_attack_pressure(const Board& board, Color perspective) noexcept;

// Returns centipawns from the side-to-move perspective.
int evaluate(const Board& board) noexcept;

}  // namespace hebichess
