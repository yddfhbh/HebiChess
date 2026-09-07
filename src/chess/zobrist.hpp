#pragma once

#include "chess/board.hpp"

namespace hebichess {

ZobristKey piece_zobrist(Piece piece, Square square) noexcept;
ZobristKey side_zobrist() noexcept;
ZobristKey castling_zobrist(CastlingRights rights) noexcept;
ZobristKey en_passant_zobrist(Square square) noexcept;

}  // namespace hebichess
