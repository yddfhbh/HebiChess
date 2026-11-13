#include "chess/eval.hpp"
#include "chess/search_profile.hpp"
#include "chess/nnue.hpp"

#include <algorithm>
#include <array>
#include <bit>
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
        score[color] += static_cast<int>(std::popcount(
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

  // V493_ENDGAME_PASSED_PAWN_SAFETY_EXPERIMENT
  // CHAOS-only passed pawn bonus (V492 base). V493 conservatively discounts
  // promotion lanes occupied farther ahead and opponent-king interception
  // in pawn-only endings. These are generic position features, not opening,
  // move or FEN special cases. HCE and NNUE are unchanged. Experimental.
  int endgame_passer_potential(Color perspective) const noexcept {
    if (phase >= 24) return 0;
    constexpr std::array<int, 8> kAdvanceBonus{0, 0, 0, 0, 10, 30, 75, 0};
    int scores[kColors]{};
    for (int color = 0; color < kColors; ++color) {
      for (int i = 0; i < piece_count[color]; ++i) {
        const Square pawn = pieces[color][i];
        if (board.piece_at(pawn).type != PieceType::Pawn) continue;
        const int direction = color == 0 ? 1 : -1;
        const int relative_rank = color == 0 ? pawn.rank() : 7 - pawn.rank();
        if (relative_rank < 4 || relative_rank > 6) continue;
        bool passed = true;
        for (int file = std::max(0, int(pawn.file()) - 1);
             file <= std::min(7, int(pawn.file()) + 1) && passed; ++file) {
          for (int rank = int(pawn.rank()) + direction;
               rank >= 0 && rank < 8; rank += direction) {
            if (pawn_mask[1 - color] & (Bitboard{1} << (rank * 8 + file))) {
              passed = false;
              break;
            }
          }
        }
        if (!passed) continue;
        int bonus = kAdvanceBonus[relative_rank];
        const Square next = at(pawn.file(), int(pawn.rank()) + direction);
        if (!next.is_valid()) continue;
        if (!board.piece_at(next).is_empty()) {
          bonus = bonus * 2 / 5;
        } else if ((attacks[1 - color] & (Bitboard{1} << next.index())) &&
                   !(attacks[color] & (Bitboard{1} << next.index()))) {
          bonus = bonus * 2 / 3;
        }
        const Bitboard pawn_square = Bitboard{1} << pawn.index();
        if ((attacks[1 - color] & pawn_square) && !(attacks[color] & pawn_square))
          bonus /= 2;

        // V493: a rook/queen (or any piece) farther up the promotion file
        // can make an apparently clear next square a poor conversion route.
        // The immediate blocker was already handled in V492 above.
        // One reduction only, independent of the number of blockers.
        if (board.piece_at(next).is_empty()) {
          for (int rank = int(pawn.rank()) + 2 * direction;
               rank >= 0 && rank < 8; rank += direction) {
            const Square forward = at(pawn.file(), rank);
            if (!board.piece_at(forward).is_empty()) {
              bonus = bonus * 3 / 5;
              break;
            }
          }
        }

        // V493: in king-and-pawn endings, avoid granting an unrestricted
        // passer premium when the opposing king is inside the pawn square.
        // This is a conservative estimate: pawn races, checks and own-king
        // support still require search. Do not apply in piece endings.
        const bool pawns_only =
            type_count[0][static_cast<int>(PieceType::Knight)] == 0 &&
            type_count[1][static_cast<int>(PieceType::Knight)] == 0 &&
            type_count[0][static_cast<int>(PieceType::Bishop)] == 0 &&
            type_count[1][static_cast<int>(PieceType::Bishop)] == 0 &&
            type_count[0][static_cast<int>(PieceType::Rook)] == 0 &&
            type_count[1][static_cast<int>(PieceType::Rook)] == 0 &&
            type_count[0][static_cast<int>(PieceType::Queen)] == 0 &&
            type_count[1][static_cast<int>(PieceType::Queen)] == 0;
        const Square enemy_king = kings[1 - color];
        if (pawns_only && enemy_king.is_valid()) {
          const int promotion_rank = color == 0 ? 7 : 0;
          const int steps_to_promote = 7 - relative_rank;
          const int king_distance = std::max(
              std::abs(int(enemy_king.file()) - int(pawn.file())),
              std::abs(int(enemy_king.rank()) - promotion_rank));
          const bool enemy_moves_first =
              board.side_to_move() != (color == 0 ? Color::White : Color::Black);
          if (king_distance <= steps_to_promote + int(enemy_moves_first))
            bonus /= 2;
        }
        scores[color] += bonus * (24 - phase) / 24;
      }
    }
    return signed_score(scores[0], scores[1], perspective);
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

  // V4911 EXPERIMENT: threat-sensitive queen/rook pressure on an exposed king.
  // Only a concrete queen ray into the king's surrounding squares qualifies.
  // An enemy rook and a weakened shield increase the practical risk, while
  // directly attacking that queen reduces it. Zero-sum and CHAOS-only.
  int chaos_king_infiltration_pressure(Color perspective) const noexcept {
    // Queen-and-rook middlegame attacks only; avoid disturbing technical
    // queen vs rook endings and the v4.9.10 mate-threat line.
    // Preserve v4.9.9 development choices while book/opening play is active.
    if (phase < 14 || board.fullmove_number() <= 16) return 0;
    int pressure[kColors]{};
    for (int attacker = 0; attacker < kColors; ++attacker) {
      const int defender = 1 - attacker;
      if (type_count[attacker][static_cast<int>(PieceType::Queen)] == 0 ||
          type_count[attacker][static_cast<int>(PieceType::Rook)] == 0 ||
          !kings[defender].is_valid()) continue;
      for (int i = 0; i < piece_count[attacker]; ++i) {
        const Square queen = pieces[attacker][i];
        if (board.piece_at(queen).type != PieceType::Queen) continue;
        const int zone_contact = static_cast<int>(std::popcount(
            attack_mask_for(queen, attacker) & king_zone[defender]));
        if (zone_contact == 0) continue;
        const Square king = kings[defender];
        const int distance = std::max(std::abs(int(queen.file()) - int(king.file())),
                                      std::abs(int(queen.rank()) - int(king.rank())));
        int value = 30 + 17 * std::min(3, zone_contact);
        if (shield(defender == 0 ? Color::White : Color::Black) < 0) value += 22;
        if (distance <= 2) value += 30;
        if (type_count[attacker][static_cast<int>(PieceType::Rook)] >= 2)
          value += 10;
        for (int j = 0; j < piece_count[attacker]; ++j) {
          const Square rook = pieces[attacker][j];
          if (board.piece_at(rook).type == PieceType::Rook &&
              (attack_mask_for(rook, attacker) & (Bitboard{1} << queen.index()))) {
            value += 25;
            break;
          }
        }
        if (attacks[defender] & (Bitboard{1} << queen.index())) value /= 4;
        pressure[attacker] += std::min(165, value);
      }
    }
    return signed_score(pressure[0], pressure[1], perspective);
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

  // V47_TACTICAL_COMPLICATIONS
  // Rewards *our* coordinated contacts against defended opposing pieces;
  // a meaningless, equal trade often removes that attacking potential.
  // Symmetric positions score zero; merely having more pieces does not
  // generate a bonus. Immediate undefended attacks are valued elsewhere.
  int tactical_complications(Color perspective) const noexcept {
    if (phase < 8) return 0;
    int value[kColors]{};
    for (int color = 0; color < kColors; ++color) {
      const int enemy = 1 - color;
      Bitboard defended_targets = 0;
      for (int i = 0; i < piece_count[enemy]; ++i) {
        const Square sq = pieces[enemy][i];
        const Piece p = board.piece_at(sq);
        if (p.type != PieceType::Knight && p.type != PieceType::Bishop &&
            p.type != PieceType::Rook && p.type != PieceType::Queen) continue;
        const Bitboard bit = Bitboard{1} << sq.index();
        if (attacks[enemy] & bit) defended_targets |= bit;
      }

      Bitboard unique_contacts = 0;
      int contact_lines = 0;
      int king_ring_attackers = 0;
      for (int i = 0; i < piece_count[color]; ++i) {
        const Square from = pieces[color][i];
        const Piece attacker = board.piece_at(from);
        if (attacker.type != PieceType::Knight &&
            attacker.type != PieceType::Bishop &&
            attacker.type != PieceType::Rook &&
            attacker.type != PieceType::Queen) continue;
        const Bitboard attack = attack_mask_for(from, color);
        if (attack & king_zone[enemy]) ++king_ring_attackers;
        const Bitboard from_bit = Bitboard{1} << from.index();
        // Do not reward an undefended piece merely because it attacks.
        if ((attacks[enemy] & from_bit) && !(attacks[color] & from_bit))
          continue;
        const Bitboard contacts = attack & defended_targets;
        unique_contacts |= contacts;
        contact_lines += std::popcount(contacts);
      }
      const int targets = std::popcount(unique_contacts);
      if (targets == 0) continue;
      const int pressure = std::min(4, targets) * 9 +
                           std::min(4, contact_lines - targets) * 5;
      const int coordination = king_ring_attackers >= 2 ? 12 :
                               king_ring_attackers == 1 ? 4 : 0;
      value[color] = std::min(65, pressure + coordination);
    }
    // Preserve endgame clarity: the positional style matters mainly when
    // multiple pieces can keep mutual threats on the board.
    return signed_score(value[0], value[1], perspective) * phase / 24;
  }

  // V49_ATTACK_PREPARATION
  // Rewards a *coordinated* two-piece staging net around the enemy king.
  // Staging is not a reward for merely retaining pieces or chasing checks.
  // Unsafe hanging attackers and one-piece queen adventures score nothing.
  int attack_preparation(Color perspective) const noexcept {
    if (phase < 12) return 0;
    int value[kColors]{};
    for (int color = 0; color < kColors; ++color) {
      const int enemy = 1 - color;
      if (!kings[enemy].is_valid()) continue;
      const int king_file = kings[enemy].file();
      const int king_rank = kings[enemy].rank();
      int staging = 0;
      int supporting = 0;
      for (int i = 0; i < piece_count[color]; ++i) {
        const Square from = pieces[color][i];
        const Piece piece = board.piece_at(from);
        if (piece.type != PieceType::Knight &&
            piece.type != PieceType::Bishop &&
            piece.type != PieceType::Rook &&
            piece.type != PieceType::Queen) continue;
        const Bitboard location = Bitboard{1} << from.index();
        if ((attacks[enemy] & location) && !(attacks[color] & location))
          continue;
        const Bitboard ray = attack_mask_for(from, color);
        int approach = 0;
        for (int df = -2; df <= 2; ++df) {
          for (int dr = -2; dr <= 2; ++dr) {
            if (std::max(std::abs(df), std::abs(dr)) != 2)
              continue;
            const Square target = at(king_file + df, king_rank + dr);
            if (target.is_valid() && (ray & (Bitboard{1} << target.index())))
              ++approach;
          }
        }
        if (approach > 0) {
          ++staging;
          if (attacks[color] & location) ++supporting;
        }
      }
      // A single piece outside the king ring is not an attacking plan.
      if (staging >= 2 && supporting >= 1)
        value[color] = std::min(24, 8 + (staging - 2) * 6 + supporting * 4);
    }
    return signed_score(value[0], value[1], perspective) * phase / 24;
  }

  // V497: score the *quality* of a minor-piece formation, not merely its
  // count.  A knight/bishop group receives value for safe mobility, central
  // influence, mutual support and overlapping control.  Hanging minors and
  // an exposed king suppress the bonus.  This is deliberately bounded and
  // CHAOS-only; ordinary HCE scores remain bit-identical.
  int minor_coordination(int color) const noexcept {
    const int enemy = 1 - color;
    const int bishops = type_count[color][static_cast<int>(PieceType::Bishop)];
    const int knights = type_count[color][static_cast<int>(PieceType::Knight)];
    const int minors = bishops + knights;
    // The V497 feature targets post-exchange material imbalances.  In the
    // untouched full-material opening (phase 24), ordinary development and
    // tactical terms already handle piece activity; skip the extra per-minor
    // mobility map until at least one non-pawn exchange changes the phase.
    if (minors < 2 || phase >= 24) return 0;

    constexpr Bitboard kCenter =
        (Bitboard{1} << 27) | (Bitboard{1} << 28) |
        (Bitboard{1} << 35) | (Bitboard{1} << 36);
    std::array<std::uint8_t, 64> coverage{};
    int supported = 0;
    int safe_mobility = 0;
    int central_control = 0;
    int forward_control = 0;
    int hanging_minors = 0;

    for (int i = 0; i < piece_count[color]; ++i) {
      const Square from = pieces[color][i];
      const Piece piece = board.piece_at(from);
      if (piece.type != PieceType::Knight && piece.type != PieceType::Bishop)
        continue;

      const Bitboard from_bit = Bitboard{1} << from.index();
      const bool defended = (attacks[color] & from_bit) != 0;
      const bool attacked = (attacks[enemy] & from_bit) != 0;
      if (defended) ++supported;
      if (attacked && !defended) ++hanging_minors;

      Bitboard targets = attack_mask_for(from, color);
      while (targets != 0) {
        const int index = std::countr_zero(targets);
        targets &= targets - 1;
        const Bitboard target_bit = Bitboard{1} << index;
        const Square target = Square::from_index(static_cast<std::uint8_t>(index));
        const Piece occupant = board.piece_at(target);
        if (!occupant.is_empty() && occupant.color == static_cast<Color>(color))
          continue;

        if (coverage[index] < 255) ++coverage[index];
        if (!(attacks[enemy] & target_bit) || (attacks[color] & target_bit))
          ++safe_mobility;
        if (kCenter & target_bit) ++central_control;
        if ((color == 0 && target.rank() >= 4) ||
            (color == 1 && target.rank() <= 3))
          ++forward_control;
      }
    }

    int overlapping_control = 0;
    for (int index = 0; index < 64; ++index) {
      if (coverage[index] >= 2) {
        const Piece occupant = board.piece_at(
            Square::from_index(static_cast<std::uint8_t>(index)));
        if (occupant.is_empty() || occupant.color != static_cast<Color>(color))
          overlapping_control += std::min<int>(2, coverage[index] - 1);
      }
    }

    int pair_bonus = 0;
    if (bishops > 0 && knights > 0) pair_bonus += 8;
    if (knights >= 2) pair_bonus += 3;
    if (minors >= 3) pair_bonus += 4;

    const int support_bonus = std::min(3, supported) * 4;
    const int mobility_bonus = std::min(16, safe_mobility);
    const int center_bonus = std::min(8, central_control) * 2;
    const int forward_bonus = std::min(8, forward_control);
    const int overlap_bonus = std::min(6, overlapping_control) * 2;
    const int king_exposure = std::popcount(king_zone[color] & attacks[enemy]);
    const int king_penalty = std::min(18, king_exposure * 2);
    const int hanging_penalty = std::min(20, hanging_minors * 7);

    const int raw = std::clamp(pair_bonus + support_bonus + mobility_bonus +
        center_bonus + forward_bonus + overlap_bonus - king_penalty -
        hanging_penalty, 0, 60);
    // Coordination is useful in every phase, but its relative value grows as
    // the board clears and the minor pieces gain durable outposts.
    const int phase_scale = 65 + (24 - phase) * 35 / 24;
    return raw * phase_scale / 100;
  }

  // V497: material-composition premium is intentionally modest because the
  // normal material term already values N/B/R/Q.  It recognizes only common
  // imbalances where coordinated minors can realistically challenge a heavy
  // piece: three minors vs queen, rook+minor(s) vs queen, or two minors vs rook.
  // The premium is conditional on the formation score above, not a blind
  // reward for sacrificing a queen.
  int material_imbalance(int color) const noexcept {
    const int enemy = 1 - color;
    const int bishops = type_count[color][static_cast<int>(PieceType::Bishop)];
    const int knights = type_count[color][static_cast<int>(PieceType::Knight)];
    const int minors = bishops + knights;
    const int own_rooks = type_count[color][static_cast<int>(PieceType::Rook)];
    const int own_queens = type_count[color][static_cast<int>(PieceType::Queen)];
    const int enemy_rooks = type_count[enemy][static_cast<int>(PieceType::Rook)];
    const int enemy_queens = type_count[enemy][static_cast<int>(PieceType::Queen)];
    if (own_queens != 0 || minors == 0) return 0;

    const int coordination = minor_coordination(color);
    int bonus = 0;
    if (enemy_queens > 0) {
      if (minors >= 3)
        bonus += 8 + std::min(12, coordination / 3);
      if (own_rooks > 0)
        bonus += 5 + std::min(8, coordination / 4);
    }
    if (own_rooks == 0 && enemy_rooks > 0 && minors >= 2)
      bonus += 4 + std::min(8, coordination / 4);
    return std::min(28, bonus);
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
  profile_add(ProfileCounter::HceEvaluationCalls);
  return evaluate_breakdown(board, board.side_to_move()).total;
}

// V47: preserve evaluate_hce() and all existing HCE regression scores.
int evaluate_chaos_tension(const Board& board, Color perspective) noexcept {
  return EvalContext(board).tactical_complications(perspective);
}

int evaluate_chaos_preparation(const Board& board, Color perspective) noexcept {
  return EvalContext(board).attack_preparation(perspective);
}

int evaluate_chaos_material_imbalance(const Board& board, Color perspective) noexcept {
  const EvalContext context(board);
  const int white = context.material_imbalance(0);
  const int black = context.material_imbalance(1);
  return signed_score(white, black, perspective);
}

int evaluate_chaos_minor_coordination(const Board& board, Color perspective) noexcept {
  const EvalContext context(board);
  const int white = context.minor_coordination(0);
  const int black = context.minor_coordination(1);
  return signed_score(white, black, perspective);
}

int evaluate_chaos_passer_potential(const Board& board, Color perspective) noexcept {
  return EvalContext(board).endgame_passer_potential(perspective);
}

int evaluate_chaos(const Board& board) noexcept {
  profile_add(ProfileCounter::HceEvaluationCalls);
  const EvalContext context(board);
  const Color perspective = board.side_to_move();
  // The extra preparation heuristic did not establish an out-of-sample
  // gain in v4.9. Keep its standalone diagnostic but do not alter search eval.
  const int coordination = signed_score(context.minor_coordination(0),
                                        context.minor_coordination(1), perspective);
  const int imbalance = signed_score(context.material_imbalance(0),
                                     context.material_imbalance(1), perspective);
  return context.breakdown(perspective).total +
         context.chaos_king_infiltration_pressure(perspective) +
         context.tactical_complications(perspective) +
         context.endgame_passer_potential(perspective) +
         coordination + imbalance;
}

bool eval_mode_available(EvalMode mode) noexcept {
  return mode != EvalMode::NNUE || nnue_network_available();
}

std::optional<int> evaluate_nnue(const Board& board) noexcept {
  return evaluate_nnue_network(board);
}

std::optional<int> evaluate(const Board& board, EvalMode mode) noexcept {
  profile_add(ProfileCounter::EvaluateApiCalls);
  if (mode == EvalMode::HCE) return evaluate_hce(board);
  if (mode == EvalMode::CHAOS) return evaluate_chaos(board);
  return evaluate_nnue(board);
}

int evaluate(const Board& board) noexcept {
  return evaluate_hce(board);
}

}  // namespace hebichess
