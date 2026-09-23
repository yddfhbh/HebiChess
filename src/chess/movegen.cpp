#include "chess/movegen.hpp"

#include <array>

namespace hebichess {
namespace {

thread_local MovegenAllocationProfile* active_allocation_profile = nullptr;

void append_move(std::vector<Move>& moves, const Move& move) {
#if HEBICHESS_QSEARCH_PROFILE
  const std::size_t previous_capacity = moves.capacity();
  moves.push_back(move);
  if (active_allocation_profile != nullptr && moves.capacity() != previous_capacity) {
    ++active_allocation_profile->allocations;
    if (previous_capacity != 0) ++active_allocation_profile->reallocations;
    active_allocation_profile->allocated_bytes += moves.capacity() * sizeof(Move);
  }
#else
  moves.push_back(move);
#endif
}

template <typename MoveList>
void append_move(MoveList& moves, const Move& move) {
  moves.push_back(move);
}

Square at(int file, int rank) noexcept {
  if (file < 0 || file >= 8 || rank < 0 || rank >= 8) return {};
  return Square::from_file_rank(static_cast<std::uint8_t>(file),
                                static_cast<std::uint8_t>(rank));
}

bool enemy_at(const Board& board, Square square, Color color) noexcept {
  return square.is_valid() && !board.piece_at(square).is_empty() &&
         board.piece_at(square).color != color;
}

template <typename MoveList>
void add_promotion_moves(MoveList& moves, Square from, Square to, bool capture) {
  constexpr std::array<PieceType, 4> promotions = {
      PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight};
  for (const PieceType promotion : promotions) {
    append_move(moves, {from, to, promotion,
                        capture ? MoveFlag::PromotionCapture : MoveFlag::Promotion});
  }
}

template <typename MoveList>
void add_pawn_moves(const Board& board, Square from, Color color, MoveList& moves) {
  const int direction = color == Color::White ? 1 : -1;
  const int start_rank = color == Color::White ? 1 : 6;
  const int promotion_rank = color == Color::White ? 7 : 0;
  const Square one = at(from.file(), from.rank() + direction);
  if (one.is_valid() && board.piece_at(one).is_empty()) {
    if (one.rank() == promotion_rank) {
      add_promotion_moves(moves, from, one, false);
    } else {
      append_move(moves, {from, one, PieceType::None, MoveFlag::Normal});
      const Square two = at(from.file(), from.rank() + 2 * direction);
      if (from.rank() == start_rank && two.is_valid() &&
          board.piece_at(two).is_empty()) {
        append_move(moves, {from, two, PieceType::None, MoveFlag::DoublePawnPush});
      }
    }
  }

  for (const int file_delta : {-1, 1}) {
    const Square target = at(from.file() + file_delta, from.rank() + direction);
    if (!target.is_valid()) continue;
    const bool capture = enemy_at(board, target, color);
    bool en_passant = false;
    if (!capture && target == board.en_passant_target()) {
      const Square captured = at(target.file(), from.rank());
      en_passant = captured.is_valid() &&
                   board.piece_at(captured).type == PieceType::Pawn &&
                   board.piece_at(captured).color == opposite(color);
    }
    if (capture || en_passant) {
      if (target.rank() == promotion_rank) {
        add_promotion_moves(moves, from, target, capture);
      } else {
        append_move(moves, {from, target, PieceType::None,
                            en_passant ? MoveFlag::EnPassant : MoveFlag::Capture});
      }
    }
  }
}

template <std::size_t Count, typename MoveList>
void add_sliding_moves(const Board& board, Square from, Color color,
                       const std::array<std::pair<int, int>, Count>& directions,
                       MoveList& moves) {
  for (const auto [df, dr] : directions) {
    for (int file = from.file() + df, rank = from.rank() + dr;; file += df, rank += dr) {
      const Square target = at(file, rank);
      if (!target.is_valid()) break;
      const Piece piece = board.piece_at(target);
      if (piece.is_empty()) {
        append_move(moves, {from, target});
      } else {
        if (piece.color != color)
          append_move(moves, {from, target, PieceType::None, MoveFlag::Capture});
        break;
      }
    }
  }
}

template <typename MoveList>
void add_castling(const Board& board, Square from, Color color, MoveList& moves) {
  const CastlingRights rights = board.castling_rights();
  const int rank = color == Color::White ? 0 : 7;
  const bool king_side = color == Color::White ? rights.white_king_side
                                               : rights.black_king_side;
  const bool queen_side = color == Color::White ? rights.white_queen_side
                                                 : rights.black_queen_side;
  const Square king_home = at(4, rank);
  if (from != king_home || board.piece_at(king_home) != Piece{PieceType::King, color}) return;

  auto clear = [&board](int first, int last, int r) {
    for (int file = first; file <= last; ++file)
      if (!board.piece_at(at(file, r)).is_empty()) return false;
    return true;
  };
  if (king_side && board.piece_at(at(7, rank)) == Piece{PieceType::Rook, color} &&
      clear(5, 6, rank) && !board.is_square_attacked(at(4, rank), opposite(color)) &&
      !board.is_square_attacked(at(5, rank), opposite(color)) &&
      !board.is_square_attacked(at(6, rank), opposite(color))) {
    append_move(moves, {from, at(6, rank), PieceType::None, MoveFlag::CastleKingSide});
  }
  if (queen_side && board.piece_at(at(0, rank)) == Piece{PieceType::Rook, color} &&
      clear(1, 3, rank) && !board.is_square_attacked(at(4, rank), opposite(color)) &&
      !board.is_square_attacked(at(3, rank), opposite(color)) &&
      !board.is_square_attacked(at(2, rank), opposite(color))) {
    append_move(moves, {from, at(2, rank), PieceType::None, MoveFlag::CastleQueenSide});
  }
}

}  // namespace

template <typename MoveList>
void generate_pseudo_legal_moves_impl(const Board& board, MoveList& moves) {
  moves.clear();
  const Color color = board.side_to_move();
  constexpr std::array<std::pair<int, int>, 8> knight_steps = {
      {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}}};
  constexpr std::array<std::pair<int, int>, 8> king_steps = {
      {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}}};
  constexpr std::array<std::pair<int, int>, 4> diagonals = {{{1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};
  constexpr std::array<std::pair<int, int>, 4> orthogonals = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

  for (std::uint8_t index = 0; index < Square::kSquareCount; ++index) {
    const Square from = Square::from_index(index);
    const Piece piece = board.piece_at(from);
    if (piece.is_empty() || piece.color != color) continue;
    switch (piece.type) {
      case PieceType::Pawn: add_pawn_moves(board, from, color, moves); break;
      case PieceType::Knight:
        for (const auto [df, dr] : knight_steps) {
          const Square to = at(from.file() + df, from.rank() + dr);
          if (to.is_valid() && (board.piece_at(to).is_empty() || enemy_at(board, to, color)))
            append_move(moves, {from, to, PieceType::None,
                                board.piece_at(to).is_empty() ? MoveFlag::Normal : MoveFlag::Capture});
        }
        break;
      case PieceType::Bishop: add_sliding_moves(board, from, color, diagonals, moves); break;
      case PieceType::Rook: add_sliding_moves(board, from, color, orthogonals, moves); break;
      case PieceType::Queen: {
        add_sliding_moves(board, from, color, diagonals, moves);
        add_sliding_moves(board, from, color, orthogonals, moves);
        break;
      }
      case PieceType::King:
        for (const auto [df, dr] : king_steps) {
          const Square to = at(from.file() + df, from.rank() + dr);
          if (to.is_valid() && (board.piece_at(to).is_empty() || enemy_at(board, to, color)))
            append_move(moves, {from, to, PieceType::None,
                                board.piece_at(to).is_empty() ? MoveFlag::Normal : MoveFlag::Capture});
        }
        add_castling(board, from, color, moves);
        break;
      case PieceType::None: break;
    }
  }
}

template <typename MoveList>
void generate_pseudo_legal_tactical_moves_impl(const Board& board, MoveList& moves) {
  moves.clear();
  const Color color = board.side_to_move();
  constexpr std::array<std::pair<int, int>, 8> knight_steps = {
      {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}}};
  constexpr std::array<std::pair<int, int>, 8> king_steps = {
      {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}}};
  constexpr std::array<std::pair<int, int>, 4> diagonals = {{{1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};
  constexpr std::array<std::pair<int, int>, 4> orthogonals = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
  const auto add_sliding_captures = [&](Square from,
                                        const auto& directions) {
    for (const auto [df, dr] : directions) {
      for (int file = from.file() + df, rank = from.rank() + dr;; file += df, rank += dr) {
        const Square target = at(file, rank);
        if (!target.is_valid()) break;
        const Piece target_piece = board.piece_at(target);
        if (target_piece.is_empty()) continue;
        if (target_piece.color != color)
          append_move(moves, {from, target, PieceType::None, MoveFlag::Capture});
        break;
      }
    }
  };
  for (std::uint8_t index = 0; index < Square::kSquareCount; ++index) {
    const Square from = Square::from_index(index);
    const Piece piece = board.piece_at(from);
    if (piece.is_empty() || piece.color != color) continue;
    switch (piece.type) {
      case PieceType::Pawn: {
        const int direction = color == Color::White ? 1 : -1;
        const int promotion_rank = color == Color::White ? 7 : 0;
        const Square one = at(from.file(), from.rank() + direction);
        if (one.is_valid() && one.rank() == promotion_rank && board.piece_at(one).is_empty())
          add_promotion_moves(moves, from, one, false);
        for (const int file_delta : {-1, 1}) {
          const Square target = at(from.file() + file_delta, from.rank() + direction);
          if (!target.is_valid()) continue;
          const bool capture = enemy_at(board, target, color);
          const Square captured = at(target.file(), from.rank());
          const bool en_passant = !capture && target == board.en_passant_target() &&
              captured.is_valid() && board.piece_at(captured).type == PieceType::Pawn &&
              board.piece_at(captured).color == opposite(color);
          if (!capture && !en_passant) continue;
          if (target.rank() == promotion_rank) add_promotion_moves(moves, from, target, true);
          else append_move(moves, {from, target, PieceType::None,
                                   en_passant ? MoveFlag::EnPassant : MoveFlag::Capture});
        }
        break;
      }
      case PieceType::Knight:
        for (const auto [df, dr] : knight_steps) {
          const Square to = at(from.file() + df, from.rank() + dr);
          if (enemy_at(board, to, color))
            append_move(moves, {from, to, PieceType::None, MoveFlag::Capture});
        }
        break;
      case PieceType::Bishop: add_sliding_captures(from, diagonals); break;
      case PieceType::Rook: add_sliding_captures(from, orthogonals); break;
      case PieceType::Queen:
        add_sliding_captures(from, diagonals);
        add_sliding_captures(from, orthogonals);
        break;
      case PieceType::King:
        for (const auto [df, dr] : king_steps) {
          const Square to = at(from.file() + df, from.rank() + dr);
          if (enemy_at(board, to, color))
            append_move(moves, {from, to, PieceType::None, MoveFlag::Capture});
        }
        break;
      case PieceType::None: break;
    }
  }
}

ScopedMovegenAllocationProfile::ScopedMovegenAllocationProfile(
    MovegenAllocationProfile* profile) noexcept
    : previous_(active_allocation_profile) {
#if HEBICHESS_QSEARCH_PROFILE
  active_allocation_profile = profile;
#else
  (void)profile;
#endif
}

ScopedMovegenAllocationProfile::~ScopedMovegenAllocationProfile() {
#if HEBICHESS_QSEARCH_PROFILE
  active_allocation_profile = previous_;
#endif
}

std::vector<Move> generate_pseudo_legal_moves(const Board& board) {
  std::vector<Move> moves;
  generate_pseudo_legal_moves_impl(board, moves);
  return moves;
}

std::vector<Move> generate_pseudo_legal_tactical_moves(const Board& board) {
  std::vector<Move> moves;
  generate_pseudo_legal_tactical_moves_impl(board, moves);
  return moves;
}

void generate_pseudo_legal_moves(const Board& board, FixedMoveList& moves) {
  generate_pseudo_legal_moves_impl(board, moves);
}

void generate_pseudo_legal_tactical_moves(const Board& board, FixedMoveList& moves) {
  generate_pseudo_legal_tactical_moves_impl(board, moves);
}

std::vector<Move> filter_legal_moves(Board& board, const std::vector<Move>& pseudo_moves) {
  std::vector<Move> legal_moves;
  const Color moving_color = board.side_to_move();
  for (const Move& move : pseudo_moves) {
    const UndoState undo = board.make_move(move);
    const Square king = board.find_king(moving_color);
    if (king.is_valid() && !board.is_square_attacked(king, opposite(moving_color)))
      append_move(legal_moves, move);
    board.unmake_move(move, undo);
  }
  return legal_moves;
}

void filter_legal_moves_in_place(Board& board, FixedMoveList& moves) {
  const Color moving_color = board.side_to_move();
  const std::size_t pseudo_count = moves.size();
  std::size_t legal_count = 0;
  for (std::size_t index = 0; index < pseudo_count; ++index) {
    const Move move = moves[index];
    const UndoState undo = board.make_move(move);
    const Square king = board.find_king(moving_color);
    const bool legal = king.is_valid() && !board.is_square_attacked(king, opposite(moving_color));
    board.unmake_move(move, undo);
    if (legal) moves[legal_count++] = move;
  }
  moves.truncate(legal_count);
}

std::vector<Move> generate_legal_moves(Board& board) {
  return filter_legal_moves(board, generate_pseudo_legal_moves(board));
}

std::vector<Move> generate_legal_tactical_moves(Board& board) {
  return filter_legal_moves(board, generate_pseudo_legal_tactical_moves(board));
}

namespace {

std::vector<LegalMoveWithCheck> generate_legal_moves_with_check_impl(
    Board& board, const std::vector<Move>& pseudo_moves) {
  std::vector<LegalMoveWithCheck> legal_moves;
  legal_moves.reserve(pseudo_moves.size());
  const Color moving_color = board.side_to_move();
  for (const Move& move : pseudo_moves) {
    const UndoState undo = board.make_move(move);
    const Square own_king = board.find_king(moving_color);
    const bool legal = own_king.is_valid() &&
        !board.is_square_attacked(own_king, opposite(moving_color));
    // This intentionally uses the fully mutated child board and the same
    // attack query as gives_check().  It therefore covers discovered checks,
    // promotion checks, and en-passant discoveries without approximation.
    const Square opponent_king = board.find_king(board.side_to_move());
    const bool gives_check = legal && opponent_king.is_valid() &&
        board.is_square_attacked(opponent_king, moving_color);
    board.unmake_move(move, undo);
    if (legal) legal_moves.push_back({move, gives_check});
  }
  return legal_moves;
}

}  // namespace

std::vector<LegalMoveWithCheck> generate_legal_moves_with_check(Board& board) {
  return generate_legal_moves_with_check_impl(board, generate_pseudo_legal_moves(board));
}

std::vector<LegalMoveWithCheck> generate_legal_tactical_moves_with_check(Board& board) {
  return generate_legal_moves_with_check_impl(board,
                                               generate_pseudo_legal_tactical_moves(board));
}

}  // namespace hebichess
