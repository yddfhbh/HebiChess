#include "chess/eval.hpp"
#include "chess/nnue.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace hebichess {
namespace {

using Bitboard = std::uint64_t;

Square at(int file, int rank) noexcept {
  if (file < 0 || file >= 8 || rank < 0 || rank >= 8) return {};
  return Square::from_file_rank(static_cast<std::uint8_t>(file),
                                static_cast<std::uint8_t>(rank));
}

int signed_score(int white, int black, Color perspective) noexcept {
  return perspective == Color::White ? white - black : black - white;
}

bool occupied_by(const Board& board, int file, int rank, Color color) noexcept {
  const Square square = at(file, rank);
  return square.is_valid() && board.piece_at(square).color == color &&
         !board.piece_at(square).is_empty();
}

int pst(PieceType type, int file, int rank, Color color, int phase) noexcept {
  if (color == Color::Black) rank = 7 - rank;
  const int center = 6 - std::abs(2 * file - 7) - std::abs(2 * rank - 7);
  switch (type) {
    case PieceType::Pawn:
      return rank * rank * 2 + (file >= 2 && file <= 5 ? 5 : 0) -
             (rank <= 1 && (file == 0 || file == 7) ? 3 : 0);
    case PieceType::Knight:
      return center * 3 - (file == 0 || file == 7 ? 12 : 0) -
             (rank == 0 || rank == 7 ? 4 : 0);
    case PieceType::Bishop: return center * 2;
    case PieceType::Rook: return (rank == 6 ? 18 : 0) +
                                  (file == 0 || file == 7 ? 0 : 2);
    case PieceType::Queen: return center - (rank < 2 ? 4 : 0);
    case PieceType::King: return phase < 10 ? center * 2 : -center;
    default: return 0;
  }
}

struct EvalContext {
  static constexpr int kColors = 2;
  static constexpr int kTypes = 7;
  static constexpr int kMaxPieces = 16;

  const Board& board;
  std::array<std::array<Square, kMaxPieces>, kColors> pieces{};
  std::array<int, kColors> piece_count{};
  int type_count[kColors][kTypes]{};
  int pawn_count[kColors][8]{};
  std::uint8_t pawn_rank_files[kColors][8]{};
  Bitboard pawn_mask[kColors]{};
  Bitboard attacks[kColors]{};
  Bitboard king_zone[kColors]{};
  Square kings[kColors]{};
  int material[kColors]{};
  int pst_score[kColors]{};
  int phase{0};

  explicit EvalContext(const Board& position) noexcept : board(position) {
    for (std::uint8_t index = 0; index < Square::kSquareCount; ++index) {
      const Square square = Square::from_index(index);
      const Piece piece = board.piece_at(square);
      if (piece.is_empty()) continue;
      const int color = piece.color == Color::White ? 0 : 1;
      pieces[color][piece_count[color]++] = square;
      ++type_count[color][static_cast<int>(piece.type)];
      material[color] += piece_value(piece.type);
      pst_score[color] += pst(piece.type, square.file(), square.rank(),
                              piece.color, phase);
      if (piece.type == PieceType::King) kings[color] = square;
      if (piece.type == PieceType::Pawn) {
        ++pawn_count[color][square.file()];
        pawn_rank_files[color][square.rank()] |=
            static_cast<std::uint8_t>(1u << square.file());
        pawn_mask[color] |= Bitboard{1} << square.index();
      }
    }
    phase = std::min(24, type_count[0][static_cast<int>(PieceType::Queen)] * 4 +
                             type_count[1][static_cast<int>(PieceType::Queen)] * 4 +
                             type_count[0][static_cast<int>(PieceType::Rook)] * 2 +
                             type_count[1][static_cast<int>(PieceType::Rook)] * 2 +
                             type_count[0][static_cast<int>(PieceType::Bishop)] +
                             type_count[1][static_cast<int>(PieceType::Bishop)] +
                             type_count[0][static_cast<int>(PieceType::Knight)] +
                             type_count[1][static_cast<int>(PieceType::Knight)]);
    // PST depends on phase, so calculate it after the single board scan.
    pst_score[0] = 0;
    pst_score[1] = 0;
    for (int color = 0; color < kColors; ++color) {
      for (int i = 0; i < piece_count[color]; ++i) {
        const Square square = pieces[color][i];
        const Piece piece = board.piece_at(square);
        pst_score[color] += pst(piece.type, square.file(), square.rank(),
                                piece.color, phase);
      }
    }
    for (int color = 0; color < kColors; ++color) {
      for (int i = 0; i < piece_count[color]; ++i) {
        attacks[color] |= attack_mask_for(pieces[color][i], color);
      }
      if (kings[color].is_valid()) {
        const int file = kings[color].file();
        const int rank = kings[color].rank();
        for (int df = -1; df <= 1; ++df) {
          for (int dr = -1; dr <= 1; ++dr) {
            const Square square = at(file + df, rank + dr);
            if (square.is_valid()) king_zone[color] |= Bitboard{1} << square.index();
          }
        }
      }
    }
  }

  Bitboard attack_mask_for(Square from, int color) const noexcept {
    const Piece piece = board.piece_at(from);
    const int file = from.file();
    const int rank = from.rank();
    Bitboard result = 0;
    auto mark = [&](int target_file, int target_rank) {
      const Square target = at(target_file, target_rank);
      if (target.is_valid()) result |= Bitboard{1} << target.index();
    };
    if (piece.type == PieceType::Pawn) {
      const int direction = color == 0 ? 1 : -1;
      mark(file - 1, rank + direction);
      mark(file + 1, rank + direction);
      return result;
    }
    if (piece.type == PieceType::Knight || piece.type == PieceType::King) {
      static constexpr int knight_steps[8][2] = {
          {1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
      static constexpr int king_steps[8][2] = {
          {-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
      const auto& steps = piece.type == PieceType::Knight ? knight_steps : king_steps;
      const int count = 8;
      for (int i = 0; i < count; ++i) mark(file + steps[i][0], rank + steps[i][1]);
      return result;
    }
    static constexpr int directions[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    const int first = piece.type == PieceType::Bishop ? 4 : 0;
    const int last = piece.type == PieceType::Rook ? 4 : 8;
    for (int direction = first; direction < last; ++direction) {
      for (int f = file + directions[direction][0], r = rank + directions[direction][1];;
           f += directions[direction][0], r += directions[direction][1]) {
        const Square target = at(f, r);
        if (!target.is_valid()) break;
        result |= Bitboard{1} << target.index();
        if (!board.piece_at(target).is_empty()) break;
      }
    }
    return result;
  }

  int mobility(int color) const noexcept {
    int total = 0;
    static constexpr int directions[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    static constexpr int knight_steps[8][2] = {
        {1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
    for (int i = 0; i < piece_count[color]; ++i) {
      const Square from = pieces[color][i];
      const Piece piece = board.piece_at(from);
      if (piece.type == PieceType::Knight) {
        for (const auto& step : knight_steps) {
          const Square target = at(from.file() + step[0], from.rank() + step[1]);
          if (target.is_valid() && (board.piece_at(target).is_empty() ||
                                    board.piece_at(target).color != piece.color)) ++total;
        }
      } else if (piece.type == PieceType::Bishop || piece.type == PieceType::Rook ||
                 piece.type == PieceType::Queen) {
        const int first = piece.type == PieceType::Bishop ? 4 : 0;
        const int last = piece.type == PieceType::Rook ? 4 : 8;
        for (int direction = first; direction < last; ++direction) {
          for (int f = from.file() + directions[direction][0],
                   r = from.rank() + directions[direction][1];;
               f += directions[direction][0], r += directions[direction][1]) {
            const Square target = at(f, r);
            if (!target.is_valid()) break;
            const Piece occupant = board.piece_at(target);
            if (occupant.is_empty()) ++total;
            else {
              if (occupant.color != piece.color) ++total;
              break;
            }
          }
        }
      }
    }
    return total;
  }

  int pawns(Color perspective) const noexcept {
    int score[kColors]{};
    for (int color = 0; color < kColors; ++color) {
      for (int file = 0; file < 8; ++file) {
        const int count = pawn_count[color][file];
        if (count > 1) score[color] -= (count - 1) * 10;
        if ((file == 0 || file == 7) && count != 0) {
          for (int rank = 0; rank < 8; ++rank) {
            if (!(pawn_rank_files[color][rank] & (1u << file))) continue;
            const int relative_rank = color == 0 ? rank : 7 - rank;
            if (relative_rank > 1) score[color] -= (relative_rank - 1) * 4;
          }
        }
        const bool has_left = file > 0 && pawn_count[color][file - 1] != 0;
        const bool has_right = file < 7 && pawn_count[color][file + 1] != 0;
        if (count && !has_left && !has_right) score[color] -= 12;
      }
    }
    for (int color = 0; color < kColors; ++color) {
      for (int rank = 0; rank < 8; ++rank) {
        const std::uint8_t files = pawn_rank_files[color][rank];
        score[color] += static_cast<int>(__builtin_popcount(
            static_cast<unsigned int>(files & (files << 1)))) * 8;
      }
    }
    return signed_score(score[0], score[1], perspective);
  }

  int passed(Color perspective) const noexcept {
    int score[kColors]{};
    for (int color = 0; color < kColors; ++color) {
      for (int i = 0; i < piece_count[color]; ++i) {
        const Square pawn = pieces[color][i];
        if (board.piece_at(pawn).type != PieceType::Pawn) continue;
        const int direction = color == 0 ? 1 : -1;
        bool is_passed = true;
        for (int file = std::max(0, int(pawn.file()) - 1);
             file <= std::min(7, int(pawn.file()) + 1); ++file) {
          for (int rank = int(pawn.rank()) + direction; rank >= 0 && rank < 8;
               rank += direction) {
            if (pawn_mask[1 - color] &
                (Bitboard{1} << (rank * 8 + file))) is_passed = false;
          }
        }
        if (is_passed) {
          const int relative_rank = color == 0 ? pawn.rank() : 7 - pawn.rank();
          score[color] += relative_rank * relative_rank + relative_rank * 3;
        }
      }
    }
    return signed_score(score[0], score[1], perspective);
  }

  int rooks(Color perspective) const noexcept {
    int score[kColors]{};
    for (int color = 0; color < kColors; ++color) {
      for (int i = 0; i < piece_count[color]; ++i) {
        const Square rook = pieces[color][i];
        if (board.piece_at(rook).type != PieceType::Rook) continue;
        const int file = rook.file();
        score[color] += pawn_count[color][file] ? 0 :
                        pawn_count[1 - color][file] ? 10 : 18;
        if ((color == 0 ? rook.rank() : 7 - rook.rank()) == 6) score[color] += 14;
      }
    }
    return signed_score(score[0], score[1], perspective);
  }

  int shield(Color color) const noexcept {
    const Square king = kings[color == Color::White ? 0 : 1];
    if (!king.is_valid()) return -40;
    int score = 0;
    const int direction = color == Color::White ? 1 : -1;
    for (int file = std::max(0, int(king.file()) - 1);
         file <= std::min(7, int(king.file()) + 1); ++file) {
      if (occupied_by(board, file, int(king.rank()) + direction, color)) score += 8;
      else if (occupied_by(board, file, int(king.rank()) + 2 * direction, color)) score += 3;
      else score -= 10;
    }
    return score;
  }

  int open_lines(Color color) const noexcept {
    const Square king = kings[color == Color::White ? 0 : 1];
    if (!king.is_valid()) return -20;
    int score = 0;
    const int ci = color == Color::White ? 0 : 1;
    for (int file = 0; file < 8; ++file) {
      if (pawn_count[ci][file] != 0) continue;
      if (pawn_count[1 - ci][file] == 0) score -= 4;
      for (int i = 0; i < piece_count[1 - ci]; ++i) {
        const Square square = pieces[1 - ci][i];
        const Piece piece = board.piece_at(square);
        if (square.file() == file && (piece.type == PieceType::Rook ||
                                      piece.type == PieceType::Queen)) score -= 6;
      }
    }
    return score;
  }

  int king_safety(Color perspective) const noexcept {
    auto castled = [&](Color color) {
      const int ci = color == Color::White ? 0 : 1;
      const int rank = color == Color::White ? 0 : 7;
      const Square king = kings[ci];
      if (!king.is_valid() || (king.file() != 2 && king.file() != 6) ||
          king.rank() != rank) return 0;
      const int rook_file = king.file() == 6 ? 5 : 3;
      return occupied_by(board, rook_file, rank, color) ? 14 : 0;
    };
    auto central_exposure = [&](Color color) {
      const Square king = kings[color == Color::White ? 0 : 1];
      if (!king.is_valid() || (king.file() != 3 && king.file() != 4) || phase < 18)
        return 0;
      return (pawn_count[color == Color::White ? 0 : 1][3] == 0 ||
              pawn_count[color == Color::White ? 0 : 1][4] == 0) ? -10 : -4;
    };
    const int white = shield(Color::White) + open_lines(Color::White) +
                      castled(Color::White) + central_exposure(Color::White);
    const int black = shield(Color::Black) + open_lines(Color::Black) +
                      castled(Color::Black) + central_exposure(Color::Black);
    const int scale = std::max(5, phase);
    return signed_score(white * scale / 24, black * scale / 24, perspective);
  }

  int king_attack(Color perspective) const noexcept {
    constexpr int weights[] = {0, 1, 6, 5, 7, 10, 0};
    int values[kColors]{};
    for (int color = 0; color < kColors; ++color) {
      int non_pawn_attackers = 0;
      for (int i = 0; i < piece_count[color]; ++i) {
        const Square square = pieces[color][i];
        const Piece piece = board.piece_at(square);
        if (king_zone[1 - color] & attack_mask_for(square, color)) {
          if (piece.type != PieceType::Pawn) ++non_pawn_attackers;
          values[color] += weights[static_cast<int>(board.piece_at(square).type)];
        }
      }
      const int multiplier = non_pawn_attackers <= 1 ? 1 :
                             non_pawn_attackers == 2 ? 12 :
                             non_pawn_attackers == 3 ? 16 : 20;
      values[color] = values[color] * multiplier / 10 +
                      (non_pawn_attackers >= 2 ? non_pawn_attackers * 3 : 0);
      if (phase >= 18) {
        int developed = 0;
        const int ci = color;
        for (int i = 0; i < piece_count[ci]; ++i) {
          const Square square = pieces[ci][i];
          const Piece piece = board.piece_at(square);
          if (piece.type != PieceType::Knight && piece.type != PieceType::Bishop) continue;
          const bool initial = piece.type == PieceType::Knight
              ? (color == 0 ? square == at(1, 0) || square == at(6, 0)
                            : square == at(1, 7) || square == at(6, 7))
              : (color == 0 ? square == at(2, 0) || square == at(5, 0)
                            : square == at(2, 7) || square == at(5, 7));
          if (!initial && !(piece.type == PieceType::Knight &&
                            (square.file() == 0 || square.file() == 7))) ++developed;
        }
        const int scale = developed == 0 ? 4 : developed == 1 ? 6 : developed == 2 ? 8 : 10;
        values[color] = values[color] * scale / 10;
      }
    }
    return signed_score(values[0], values[1], perspective);
  }

  int space(Color perspective) const noexcept {
    int score[kColors]{};
    for (int square = 0; square < 64; ++square) {
      const int file = square % 8;
      const int rank = square / 8;
      const int weight = file >= 2 && file <= 5 ? 2 : (file == 1 || file == 6 ? 1 : 0);
      if ((attacks[0] & (Bitboard{1} << square)) && rank >= 4) score[0] += weight;
      if ((attacks[1] & (Bitboard{1} << square)) && rank <= 3) score[1] += weight;
    }
    return signed_score(score[0], score[1], perspective);
  }

  int development(Color perspective) const noexcept {
    int score[kColors]{};
    for (int color = 0; color < kColors; ++color) {
      for (int i = 0; i < piece_count[color]; ++i) {
        const Square square = pieces[color][i];
        const Piece piece = board.piece_at(square);
        if (piece.type != PieceType::Knight && piece.type != PieceType::Bishop) continue;
        const bool initial = piece.type == PieceType::Knight
            ? (color == 0 ? square == at(1, 0) || square == at(6, 0)
                          : square == at(1, 7) || square == at(6, 7))
            : (color == 0 ? square == at(2, 0) || square == at(5, 0)
                          : square == at(2, 7) || square == at(5, 7));
        if (initial) score[color] -= 6;
        else if (piece.type == PieceType::Knight && (square.file() == 0 || square.file() == 7))
          score[color] += 0;
        else score[color] += 8;
      }
    }
    return signed_score(score[0] * phase / 24, score[1] * phase / 24, perspective);
  }

  int threats(Color perspective) const noexcept {
    int score[kColors]{};
    for (int color = 0; color < kColors; ++color) {
      for (int i = 0; i < piece_count[color]; ++i) {
        const Square square = pieces[color][i];
        const Piece piece = board.piece_at(square);
        if (piece.type == PieceType::Pawn || piece.type == PieceType::King) continue;
        const Bitboard target = Bitboard{1} << square.index();
        if ((attacks[1 - color] & target) && !(attacks[color] & target))
          score[1 - color] += piece_value(piece.type) / 40;
      }
    }
    return signed_score(score[0], score[1], perspective);
  }

  EvalBreakdown breakdown(Color perspective) const noexcept {
    EvalBreakdown result;
    result.material = signed_score(material[0], material[1], perspective);
    result.pst = signed_score(pst_score[0], pst_score[1], perspective);
    result.mobility = signed_score(mobility(0) * 2, mobility(1) * 2, perspective);
    result.pawns = pawns(perspective);
    result.passed_pawns = passed(perspective);
    result.bishop_pair = signed_score(type_count[0][static_cast<int>(PieceType::Bishop)] >= 2 ? 30 : 0,
                                      type_count[1][static_cast<int>(PieceType::Bishop)] >= 2 ? 30 : 0,
                                      perspective);
    result.rook_activity = rooks(perspective);
    result.king_safety = king_safety(perspective);
    result.king_attack = king_attack(perspective);
    result.space = space(perspective);
    result.threats = threats(perspective);
    result.initiative = perspective == Color::White
                            ? (board.side_to_move() == Color::White ? 10 : -10)
                            : (board.side_to_move() == Color::White ? -10 : 10);
    result.development = development(perspective);
    result.total = result.material + result.pst + result.mobility + result.pawns +
                   result.passed_pawns + result.bishop_pair + result.rook_activity +
                   result.king_safety + result.king_attack + result.space +
                   result.threats + result.initiative + result.development;
    return result;
  }
};

}  // namespace

int piece_value(PieceType type) noexcept {
  switch (type) {
    case PieceType::Pawn: return 100;
    case PieceType::Knight: return 320;
    case PieceType::Bishop: return 330;
    case PieceType::Rook: return 500;
    case PieceType::Queen: return 900;
    default: return 0;
  }
}

int game_phase(const Board& board) noexcept { return EvalContext(board).phase; }

int evaluate_material(const Board& board, Color perspective) noexcept {
  const EvalContext context(board);
  return signed_score(context.material[0], context.material[1], perspective);
}

int evaluate_piece_square(const Board& board, Color perspective) noexcept {
  const EvalContext context(board);
  return signed_score(context.pst_score[0], context.pst_score[1], perspective);
}

int evaluate_piece_activity(const Board& board, Color perspective) noexcept {
  return evaluate_piece_square(board, perspective);
}

int evaluate_mobility(const Board& board, Color perspective) noexcept {
  const EvalContext context(board);
  return signed_score(context.mobility(0) * 2, context.mobility(1) * 2, perspective);
}

int evaluate_pawn_structure(const Board& board, Color perspective) noexcept {
  return EvalContext(board).pawns(perspective);
}

int evaluate_passed_pawns(const Board& board, Color perspective) noexcept {
  return EvalContext(board).passed(perspective);
}

int evaluate_rooks(const Board& board, Color perspective) noexcept {
  return EvalContext(board).rooks(perspective);
}

int evaluate_king_safety(const Board& board, Color perspective) noexcept {
  return EvalContext(board).king_safety(perspective);
}

int evaluate_king_attack(const Board& board, Color perspective) noexcept {
  return EvalContext(board).king_attack(perspective);
}

int evaluate_attack_pressure(const Board& board, Color perspective) noexcept {
  return evaluate_king_attack(board, perspective);
}

int evaluate_space(const Board& board, Color perspective) noexcept {
  return EvalContext(board).space(perspective);
}

int evaluate_threats(const Board& board, Color perspective) noexcept {
  return EvalContext(board).threats(perspective);
}

int evaluate_initiative(const Board& board, Color perspective) noexcept {
  return perspective == Color::White
             ? (board.side_to_move() == Color::White ? 10 : -10)
             : (board.side_to_move() == Color::White ? -10 : 10);
}

int evaluate_development(const Board& board, Color perspective) noexcept {
  return EvalContext(board).development(perspective);
}

EvalBreakdown evaluate_breakdown(const Board& board, Color perspective) noexcept {
  return EvalContext(board).breakdown(perspective);
}

int evaluate_hce(const Board& board) noexcept {
  return evaluate_breakdown(board, board.side_to_move()).total;
}

bool eval_mode_available(EvalMode mode) noexcept {
  return mode == EvalMode::HCE || nnue_network_available();
}

std::optional<int> evaluate_nnue(const Board& board) noexcept {
  return evaluate_nnue_network(board);
}

std::optional<int> evaluate(const Board& board, EvalMode mode) noexcept {
  if (mode == EvalMode::HCE) return evaluate_hce(board);
  return evaluate_nnue(board);
}

int evaluate(const Board& board) noexcept {
  return evaluate_hce(board);
}

}  // namespace hebichess
