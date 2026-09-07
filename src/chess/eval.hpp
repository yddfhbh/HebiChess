#pragma once

#include "chess/board.hpp"

namespace hebichess {

constexpr int AGGRESSION_TOLERANCE_CP = 35;

struct EvalBreakdown {
  int material{0};
  int pst{0};
  int mobility{0};
  int pawns{0};
  int passed_pawns{0};
  int bishop_pair{0};
  int rook_activity{0};
  int king_safety{0};
  int king_attack{0};
  int space{0};
  int threats{0};
  int initiative{0};
  int development{0};
  int total{0};
};

int piece_value(PieceType type) noexcept;
int game_phase(const Board& board) noexcept; // 0 = endgame, 24 = middlegame
int evaluate_material(const Board& board, Color perspective) noexcept;
int evaluate_piece_square(const Board& board, Color perspective) noexcept;
int evaluate_piece_activity(const Board& board, Color perspective) noexcept;
int evaluate_mobility(const Board& board, Color perspective) noexcept;
int evaluate_pawn_structure(const Board& board, Color perspective) noexcept;
int evaluate_passed_pawns(const Board& board, Color perspective) noexcept;
int evaluate_rooks(const Board& board, Color perspective) noexcept;
int evaluate_king_safety(const Board& board, Color perspective) noexcept;
int evaluate_king_attack(const Board& board, Color perspective) noexcept;
int evaluate_space(const Board& board, Color perspective) noexcept;
int evaluate_threats(const Board& board, Color perspective) noexcept;
int evaluate_initiative(const Board& board, Color perspective) noexcept;
int evaluate_development(const Board& board, Color perspective) noexcept;
int evaluate_attack_pressure(const Board& board, Color perspective) noexcept;

EvalBreakdown evaluate_breakdown(const Board& board, Color perspective) noexcept;
// Returns centipawns from the side-to-move perspective.
int evaluate(const Board& board) noexcept;

}  // namespace hebichess
