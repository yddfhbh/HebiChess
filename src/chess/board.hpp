#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "chess/move.hpp"
#include "chess/types.hpp"

namespace hebichess {

using ZobristKey = std::uint64_t;

struct CastlingRights {
  bool white_king_side{false};
  bool white_queen_side{false};
  bool black_king_side{false};
  bool black_queen_side{false};

  constexpr bool operator==(const CastlingRights&) const noexcept = default;
};

struct UndoState {
  Piece captured_piece{};
  Square captured_square{};
  CastlingRights castling_rights{};
  Square en_passant_target{};
  std::uint32_t halfmove_clock{0};
  std::uint32_t fullmove_number{1};
  Color side_to_move{Color::White};
  ZobristKey previous_zobrist_key{0};
};

class Board {
 public:
  Board() noexcept;

  static Board initial() noexcept;
  static std::optional<Board> from_fen(const std::string& fen);

  std::string to_fen() const;

  bool is_square_attacked(Square square, Color by_color) const noexcept;

  Square find_king(Color color) const noexcept;
  UndoState make_move(const Move& move) noexcept;
  void unmake_move(const Move& move, const UndoState& undo) noexcept;

  constexpr const Piece& piece_at(Square square) const noexcept {
    return squares_[square.index()];
  }

  constexpr Piece& piece_at(Square square) noexcept {
    return squares_[square.index()];
  }

  constexpr void set_piece(Square square, Piece piece) noexcept {
    squares_[square.index()] = piece;
  }

  constexpr const std::array<Piece, Square::kSquareCount>& squares() const
      noexcept {
    return squares_;
  }

  constexpr Color side_to_move() const noexcept { return side_to_move_; }
  void set_side_to_move(Color color) noexcept;

  constexpr CastlingRights castling_rights() const noexcept { return castling_rights_; }
  void set_castling_rights(CastlingRights rights) noexcept;

  constexpr Square en_passant_target() const noexcept { return en_passant_target_; }
  void set_en_passant_target(Square square) noexcept;

  constexpr ZobristKey zobrist_key() const noexcept { return zobrist_key_; }

  constexpr std::uint32_t halfmove_clock() const noexcept {
    return halfmove_clock_;
  }
  constexpr void set_halfmove_clock(std::uint32_t clock) noexcept {
    halfmove_clock_ = clock;
  }

  constexpr std::uint32_t fullmove_number() const noexcept {
    return fullmove_number_;
  }
  constexpr void set_fullmove_number(std::uint32_t number) noexcept {
    fullmove_number_ = number;
  }

 private:
  std::array<Piece, Square::kSquareCount> squares_{};
  Color side_to_move_{Color::White};
  CastlingRights castling_rights_{};
  Square en_passant_target_{};
  std::uint32_t halfmove_clock_{0};
  std::uint32_t fullmove_number_{1};
  ZobristKey zobrist_key_{0};
};

UndoState make_move(Board& board, const Move& move) noexcept;
void unmake_move(Board& board, const Move& move, const UndoState& undo) noexcept;
ZobristKey compute_zobrist(const Board& board) noexcept;

}  // namespace hebichess
