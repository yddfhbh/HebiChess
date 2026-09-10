#include "chess/search.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>

#include "chess/see.hpp"

namespace hebichess {
namespace {

TranspositionTable& transposition_table() {
  static TranspositionTable table(64);
  return table;
}

constexpr int MAX_SEARCH_PLY = 128;
constexpr int MAX_HISTORY = 32768;
constexpr int ASPIRATION_INITIAL_WINDOW_CP = 35;

struct StyleProfile {
  SearchResult& result;
};

bool move_order_less(const Move& a, const Move& b) noexcept {
  if (a.from.index() != b.from.index()) return a.from.index() < b.from.index();
  if (a.to.index() != b.to.index()) return a.to.index() < b.to.index();
  if (a.promotion != b.promotion) return a.promotion < b.promotion;
  return a.flag < b.flag;
}

thread_local StyleProfile* active_style_profile = nullptr;

class StyleTimer {
 public:
  explicit StyleTimer(std::uint64_t SearchResult::*field) : field_(field) {
    if (active_style_profile != nullptr) started_ = std::chrono::steady_clock::now();
  }
  ~StyleTimer() {
    if (active_style_profile != nullptr)
      active_style_profile->result.*field_ += static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - started_).count());
  }
 private:
  std::uint64_t SearchResult::*field_;
  std::chrono::steady_clock::time_point started_{};
};

struct SearchHeuristics {
  std::array<std::array<Move, 2>, MAX_SEARCH_PLY> killers{};
  std::array<std::array<std::array<int, Square::kSquareCount>,
                         Square::kSquareCount>, 2> history{};

  void clear() noexcept {
    killers = {};
    history = {};
  }

  int history_score(Color side, const Move& move) const noexcept {
    return history[static_cast<int>(side)][move.from.index()][move.to.index()];
  }

  void update_history(Color side, const Move& move, int depth) noexcept {
    if (move.from.is_valid() && move.to.is_valid()) {
      int& score = history[static_cast<int>(side)][move.from.index()][move.to.index()];
      score = std::min(MAX_HISTORY, score + std::max(1, depth * depth));
      if (score == MAX_HISTORY) {
        for (auto& color : history)
          for (auto& from : color)
            for (int& value : from) value /= 2;
      }
    }
  }

  void store_killer(const Move& move, int ply) noexcept {
    if (ply < 0 || ply >= MAX_SEARCH_PLY) return;
    if (killers[ply][0] == move) return;
    killers[ply][1] = killers[ply][0];
    killers[ply][0] = move;
  }
};

SearchHeuristics& search_heuristics() {
  static SearchHeuristics heuristics;
  return heuristics;
}

int score_to_tt(int score, int ply) noexcept {
  if (score > MATE_SCORE - 1000) return score + ply;
  if (score < -MATE_SCORE + 1000) return score - ply;
  return score;
}

int score_from_tt(int score, int ply) noexcept {
  if (score > MATE_SCORE - 1000) return score - ply;
  if (score < -MATE_SCORE + 1000) return score + ply;
  return score;
}

struct SearchContext {
  std::uint64_t& nodes;
  std::uint64_t& qnodes;
  bool has_deadline{false};
  std::chrono::steady_clock::time_point deadline{};
  bool stopped{false};
  TranspositionTable* tt{nullptr};
  SearchResult* result{nullptr};
  bool use_see_pruning{true};
  SearchHeuristics* heuristics{nullptr};
  bool use_null_move{false};
  bool use_lmr{false};
  bool use_pvs{false};
  EvalMode eval_mode{EvalMode::HCE};
  // The main search amortizes the clock read across a small node batch.
  // Root style proofs use a separate, strict policy below.
  std::uint64_t deadline_check_interval_nodes{1};
  std::uint64_t deadline_check_calls{0};

  bool should_stop() {
    if (stopped) return true;
    if (!has_deadline) return false;
    if (deadline_check_interval_nodes > 1 &&
        ++deadline_check_calls % deadline_check_interval_nodes != 0) return false;
    if (std::chrono::steady_clock::now() >= deadline) {
      stopped = true;
    }
    return stopped;
  }
};

bool gives_check(Board& board, const Move& move) noexcept {
  if (active_style_profile != nullptr) ++active_style_profile->result.style_gives_check_calls;
  const Color mover = board.side_to_move();
  const UndoState undo = board.make_move(move);
  const Square king = board.find_king(board.side_to_move());
  const bool check = king.is_valid() && board.is_square_attacked(king, mover);
  board.unmake_move(move, undo);
  return check;
}

bool is_capture(const Move& move) noexcept {
  return move.flag == MoveFlag::Capture || move.flag == MoveFlag::EnPassant ||
         move.flag == MoveFlag::PromotionCapture;
}

int move_see(const Board& board, const Move& move, SearchResult* result) noexcept {
  if (!is_capture(move) && !move.is_promotion()) return 0;
  if (result != nullptr) ++result->see_calls;
  return static_exchange_eval(board, move);
}

bool is_quiet_move(const Move& move) noexcept {
  return !is_capture(move) && !move.is_promotion();
}

int undeveloped_minor_count(const Board& board, Color side) noexcept {
  struct StartingMinor {
    Square square;
    PieceType type;
  };
  const std::array<StartingMinor, 4> starting = side == Color::White
      ? std::array<StartingMinor, 4>{{
            {Square::from_file_rank(1, 0), PieceType::Knight},
            {Square::from_file_rank(6, 0), PieceType::Knight},
            {Square::from_file_rank(2, 0), PieceType::Bishop},
            {Square::from_file_rank(5, 0), PieceType::Bishop},
        }}
      : std::array<StartingMinor, 4>{{
            {Square::from_file_rank(1, 7), PieceType::Knight},
            {Square::from_file_rank(6, 7), PieceType::Knight},
            {Square::from_file_rank(2, 7), PieceType::Bishop},
            {Square::from_file_rank(5, 7), PieceType::Bishop},
        }};
  int undeveloped = 0;
  for (const StartingMinor& minor : starting) {
    const Piece piece = board.piece_at(minor.square);
    if (piece.color == side && piece.type == minor.type) ++undeveloped;
  }
  return undeveloped;
}

struct OrderedMove {
  Move move{};
  int see{0};
  int score{0};
  bool capture{false};
  bool gives_check{false};
};

std::vector<OrderedMove> order_moves(Board& board, const std::vector<Move>& moves,
                                     SearchResult* result,
                                     SearchHeuristics* heuristics, int ply,
                                     const std::optional<Move>& tt_move = std::nullopt,
                                     bool include_checks = false) {
  std::vector<OrderedMove> ordered;
  ordered.reserve(moves.size());
  for (const Move& move : moves) {
    OrderedMove item;
    item.move = move;
    item.capture = is_capture(move);
    item.see = move_see(board, move, result);
    // Check detection requires make/unmake and dominates NPS if performed for
    // every legal move at every main-search node.  Main search probes it only
    // for an otherwise reducible late quiet move; qsearch needs it for its
    // tactical pruning exceptions.
    item.gives_check = include_checks && gives_check(board, move);
    const Piece victim = board.piece_at(move.to);
    if (item.capture && item.see >= 0) item.score += 4000000 + item.see * 10;
    else if (move.is_promotion()) item.score += 3000000 + item.see * 10;
    else if (is_quiet_move(move) && heuristics != nullptr && ply >= 0 && ply < MAX_SEARCH_PLY) {
      if (move == heuristics->killers[ply][0]) {
        item.score += 2500000;
        if (result != nullptr) ++result->killer_uses;
      } else if (move == heuristics->killers[ply][1]) {
        item.score += 2400000;
        if (result != nullptr) ++result->killer_uses;
      } else if (heuristics->history_score(board.side_to_move(), move) > 0) {
        item.score += 2200000 + heuristics->history_score(board.side_to_move(), move);
      } else if (item.gives_check) item.score += 2000000;
    } else if (item.gives_check) {
      item.score += 2000000;
    } else if (item.capture) {
      item.score += 1000000 + item.see * 10;
    }
    if (move.flag == MoveFlag::EnPassant) item.score += 10 * piece_value(PieceType::Pawn);
    else if (!victim.is_empty())
      item.score += 10 * piece_value(victim.type) - piece_value(board.piece_at(move.from).type);
    if (tt_move.has_value() && move == *tt_move) item.score += 5000000;
    ordered.push_back(item);
  }
  std::sort(ordered.begin(), ordered.end(), [](const OrderedMove& a, const OrderedMove& b) {
    return a.score != b.score ? a.score > b.score : move_order_less(a.move, b.move);
  });
  return ordered;
}

bool has_non_pawn_material(const Board& board, Color side) noexcept {
  for (const Piece& piece : board.squares()) {
    if (piece.color == side &&
        (piece.type == PieceType::Knight || piece.type == PieceType::Bishop ||
         piece.type == PieceType::Rook || piece.type == PieceType::Queen)) {
      return true;
    }
  }
  return false;
}

int count_king_escapes(const Board& board, Color king_color) {
  Board copy = board;
  copy.set_side_to_move(king_color);
  int escapes = 0;
  for (const Move& move : generate_legal_moves(copy)) {
    if (copy.piece_at(move.from).type == PieceType::King &&
        move.flag != MoveFlag::CastleKingSide &&
        move.flag != MoveFlag::CastleQueenSide) {
      ++escapes;
    }
  }
  return escapes;
}

Square at(int file, int rank) noexcept {
  if (file < 0 || file >= 8 || rank < 0 || rank >= 8) return {};
  return Square::from_file_rank(static_cast<std::uint8_t>(file),
                                static_cast<std::uint8_t>(rank));
}

int chebyshev_distance(Square a, Square b) noexcept {
  return std::max(std::abs(static_cast<int>(a.file()) - b.file()),
                  std::abs(static_cast<int>(a.rank()) - b.rank()));
}

bool piece_attacks_square(const Board& board, Square from, Square target,
                          Color attacker) noexcept {
  const Piece piece = board.piece_at(from);
  if (piece.color != attacker || piece.is_empty()) return false;
  const int df = static_cast<int>(target.file()) - from.file();
  const int dr = static_cast<int>(target.rank()) - from.rank();
  if (piece.type == PieceType::Pawn)
    return dr == (attacker == Color::White ? 1 : -1) && std::abs(df) == 1;
  if (piece.type == PieceType::Knight) return df * df + dr * dr == 5;
  if (piece.type == PieceType::King) return std::max(std::abs(df), std::abs(dr)) == 1;
  const bool diagonal = df != 0 && std::abs(df) == std::abs(dr);
  const bool straight = (df == 0) != (dr == 0);
  if ((piece.type == PieceType::Bishop && !diagonal) ||
      (piece.type == PieceType::Rook && !straight) ||
      (piece.type == PieceType::Queen && !diagonal && !straight)) return false;
  const int step_file = df == 0 ? 0 : (df > 0 ? 1 : -1);
  const int step_rank = dr == 0 ? 0 : (dr > 0 ? 1 : -1);
  for (int file = static_cast<int>(from.file()) + step_file,
           rank = static_cast<int>(from.rank()) + step_rank;
       file != target.file() || rank != target.rank(); file += step_file, rank += step_rank) {
    if (!board.piece_at(at(file, rank)).is_empty()) return false;
  }
  return true;
}

bool attacks_king_ring(const Board& board, Square from, Color attacker,
                       Square king) noexcept {
  for (int df = -1; df <= 1; ++df) {
    for (int dr = -1; dr <= 1; ++dr) {
      if (df == 0 && dr == 0) continue;
      const Square target = at(static_cast<int>(king.file()) + df,
                               static_cast<int>(king.rank()) + dr);
      if (target.is_valid() && piece_attacks_square(board, from, target, attacker)) return true;
    }
  }
  return false;
}

struct StyleAttackState {
  int ring_control{0};
  int ring_participants{0};
  int congregation{0};
  int open_lines{0};
  int checking_motifs{0};
  int sacrifice_motifs{0};
  int threats{0};
  // Pawns directly in front of the enemy king.  This deliberately only looks
  // at the king's three neighbouring files, so a central pawn trade cannot be
  // mistaken for a king break.
  int king_shield{0};
};

StyleAttackState style_attack_state(const Board& board, Color attacker, bool before_root = false) {
  StyleTimer timer(before_root ? &SearchResult::style_before_attack_time_us
                               : &SearchResult::style_after_attack_time_us);
  StyleAttackState state;
  const Square king = board.find_king(opposite(attacker));
  if (!king.is_valid()) return state;

  {
    StyleTimer shield_timer(&SearchResult::style_king_shield_time_us);
    const Color defender = opposite(attacker);
    const int pawn_step = defender == Color::White ? 1 : -1;
    for (int file_delta = -1; file_delta <= 1; ++file_delta) {
      const int file = static_cast<int>(king.file()) + file_delta;
      if (file < 0 || file >= 8) continue;
      for (int distance = 1; distance <= 2; ++distance) {
        const Square shield = at(file, static_cast<int>(king.rank()) + pawn_step * distance);
        if (shield.is_valid() &&
            board.piece_at(shield) == Piece{PieceType::Pawn, defender}) ++state.king_shield;
      }
    }
  }

  // This deliberately does not call evaluate_king_attack(): style must not
  // count the same immediate HCE king-attack signal twice.
  for (int df = -1; df <= 1; ++df) {
    for (int dr = -1; dr <= 1; ++dr) {
      if (df == 0 && dr == 0) continue;
      const Square ring = at(static_cast<int>(king.file()) + df,
                             static_cast<int>(king.rank()) + dr);
      if (ring.is_valid() && board.is_square_attacked(ring, attacker)) ++state.ring_control;
    }
  }
  for (std::uint8_t i = 0; i < Square::kSquareCount; ++i) {
    const Square square = Square::from_index(i);
    const Piece piece = board.piece_at(square);
    if (piece.color != attacker || piece.is_empty()) continue;
    if (piece.type == PieceType::Knight || piece.type == PieceType::Bishop ||
        piece.type == PieceType::Rook || piece.type == PieceType::Queen) {
      if (attacks_king_ring(board, square, attacker, king)) ++state.ring_participants;
      // A compact, capped measure of piece concentration toward the enemy king.
      state.congregation += std::max(0, 5 - chebyshev_distance(square, king));
    }
  }
  state.congregation = std::min(12, state.congregation);

  // Open or semi-open lanes next to the enemy king are useful only when a
  // heavy piece can actually use that file or diagonal.
  for (int df : {-1, 0, 1}) {
    const int file = static_cast<int>(king.file()) + df;
    if (file < 0 || file >= 8) continue;
    bool own_pawn = false;
    bool enemy_pawn = false;
    bool heavy = false;
    for (int rank = 0; rank < 8; ++rank) {
      const Piece piece = board.piece_at(at(file, rank));
      own_pawn |= piece == Piece{PieceType::Pawn, attacker};
      enemy_pawn |= piece == Piece{PieceType::Pawn, opposite(attacker)};
      heavy |= piece.color == attacker &&
               (piece.type == PieceType::Rook || piece.type == PieceType::Queen);
    }
    if (heavy && !own_pawn) state.open_lines += enemy_pawn ? 1 : 2;
  }
  for (int df : {-1, 1}) for (int dr : {-1, 1}) {
    for (int f = static_cast<int>(king.file()) + df, r = static_cast<int>(king.rank()) + dr;
         f >= 0 && f < 8 && r >= 0 && r < 8; f += df, r += dr) {
      const Piece piece = board.piece_at(at(f, r));
      if (piece.is_empty()) continue;
      if (piece.color == attacker &&
          (piece.type == PieceType::Bishop || piece.type == PieceType::Queen)) ++state.open_lines;
      break;
    }
  }
  state.open_lines = std::min(6, state.open_lines);

  Board motif_board = board;
  motif_board.set_side_to_move(attacker);
  if (active_style_profile != nullptr) ++active_style_profile->result.style_legal_move_generations;
  for (const Move& candidate : generate_legal_moves(motif_board)) {
    if (gives_check(motif_board, candidate) && ++state.checking_motifs == 4) break;
  }
  state.threats = std::clamp(evaluate_threats(board, attacker), -20, 20);
  return state;
}

int attack_reward(const StyleAttackState& before, const StyleAttackState& after,
                  bool check) noexcept {
  const int ring = std::clamp(after.ring_control - before.ring_control, 0, 3);
  const int participants = std::clamp(after.ring_participants - before.ring_participants, 0, 2);
  const int motifs = std::clamp(after.checking_motifs - before.checking_motifs, 0, 2);
  const int lines = std::clamp(after.open_lines - before.open_lines, 0, 2);
  const int threats = std::clamp(after.threats - before.threats, 0, 8);
  return (check ? 8 : 0) + ring * 4 + participants * 4 + motifs * 3 + lines * 3 + threats / 2;
}

void populate_attack_breakdown(RootMoveInfo& info, const StyleAttackState& before,
                               const StyleAttackState& after, bool check,
                               int escape_delta) noexcept {
  info.attack_check_reward = check ? 8 : 0;
  info.attack_ring_reward = std::clamp(after.ring_control - before.ring_control, 0, 4) * 4;
  info.attack_participant_reward =
      std::clamp(after.ring_participants - before.ring_participants, 0, 3) * 4;
  info.attack_motif_reward =
      std::clamp(after.checking_motifs - before.checking_motifs, 0, 3) * 3;
  info.attack_line_reward = std::clamp(after.open_lines - before.open_lines, 0, 3) * 3;
  info.attack_threat_reward =
      std::max(0, after.threats - before.threats) / 2;
  info.attack_escape_reward = std::clamp(-escape_delta, 0, 3) * 2;
  info.shield_pawns_removed = std::max(0, before.king_shield - after.king_shield);
  info.opened_king_lines = std::max(0, after.open_lines - before.open_lines);
}

int concrete_attack_signals(const StyleAttackState& before,
                            const StyleAttackState& after, bool check,
                            int escape_delta) noexcept {
  int signals = 0;
  signals += check;
  signals += escape_delta < 0;
  signals += after.ring_control > before.ring_control;
  signals += after.ring_participants > before.ring_participants;
  signals += after.open_lines > before.open_lines;
  signals += after.king_shield < before.king_shield;
  signals += after.checking_motifs > before.checking_motifs;
  signals += after.threats > before.threats;
  return signals;
}

bool destination_can_be_captured(const Board& after, const Move& move, Color mover) {
  Board reply = after;
  reply.set_side_to_move(opposite(mover));
  for (const Move& candidate : generate_legal_moves(reply)) {
    if (is_capture(candidate) && candidate.to == move.to) return true;
  }
  return false;
}

SacrificeKind classify_sacrifice(const Board& before, const Move& move,
                                 const Board& after,
                                 const StyleAttackState& before_attack,
                                 const StyleAttackState& after_attack,
                                 bool check, int escape_delta) noexcept {
  if (active_style_profile != nullptr) ++active_style_profile->result.style_sacrifice_calculations;
  const Piece offered = before.piece_at(move.from);
  const int signals = concrete_attack_signals(before_attack, after_attack, check, escape_delta);
  if (is_capture(move)) {
    if (active_style_profile != nullptr) ++active_style_profile->result.style_see_calls;
    const int see = static_exchange_eval(before, move);
    if (see >= 0 || signals < 2) return SacrificeKind::None;
    const Piece captured = before.piece_at(move.to);
    if (offered.type == PieceType::Rook &&
        (captured.type == PieceType::Knight || captured.type == PieceType::Bishop)) {
      return SacrificeKind::ExchangeSacrifice;
    }
    if (offered.type == PieceType::Queen) return SacrificeKind::MajorSacrifice;
    return SacrificeKind::MinorOrPawnSacrifice;
  }
  if (is_quiet_move(move) && offered.type != PieceType::Pawn &&
      offered.type != PieceType::King && piece_value(offered.type) >= 300 &&
      destination_can_be_captured(after, move, before.side_to_move()) && signals >= 2 &&
      // A loose queen checking move is not a sacrifice.  Require an actual
      // shield break or opened king line before it can receive this label.
      (offered.type != PieceType::Queen ||
       after_attack.king_shield < before_attack.king_shield ||
       after_attack.open_lines > before_attack.open_lines)) {
    return offered.type == PieceType::Queen ? SacrificeKind::MajorSacrifice
                                             : SacrificeKind::MinorOrPawnSacrifice;
  }
  return SacrificeKind::None;
}

int count_sacrifice_motifs(const Board& board, Color attacker) {
  StyleTimer motif_timer(&SearchResult::style_sacrifice_motifs_time_us);
  Board motif_board = board;
  motif_board.set_side_to_move(attacker);
  const StyleAttackState before = style_attack_state(motif_board, attacker);
  int motifs = 0;
  if (active_style_profile != nullptr) ++active_style_profile->result.style_legal_move_generations;
  for (const Move& move : generate_legal_moves(motif_board)) {
    Board after = motif_board;
    if (active_style_profile != nullptr) ++active_style_profile->result.style_child_boards;
    after.make_move(move);
    Board check_probe = motif_board;
    const bool check = gives_check(check_probe, move);
    const StyleAttackState after_attack = style_attack_state(after, attacker);
    const int escapes = count_king_escapes(after, opposite(attacker)) -
                        count_king_escapes(motif_board, opposite(attacker));
    const SacrificeKind kind = classify_sacrifice(motif_board, move, after, before,
                                                  after_attack, check, escapes);
    // Preparation is only for a *next-move concrete capture* sacrifice.  Do
    // not let a quiet loose piece or a generic ring-control move manufacture
    // a motif; that was the source of early-queen false positives.
    if (is_capture(move) && active_style_profile != nullptr) ++active_style_profile->result.style_see_calls;
    const bool concrete_capture = is_capture(move) && static_exchange_eval(motif_board, move) < 0 &&
        (check || after_attack.king_shield < before.king_shield ||
         after_attack.open_lines > before.open_lines ||
         kind == SacrificeKind::ExchangeSacrifice);
    if (kind != SacrificeKind::None && concrete_capture && ++motifs == 4) break;
  }
  return motifs;
}

struct RootStyleContext {
  const Board& before;
  Color attacker;
  StyleAttackState before_attack;
  int phase;
  int undeveloped_minors;
  Square own_king;
  int before_sacrifice_motifs{-1};
};

RootStyleContext make_root_style_context(const Board& before) {
  const Color attacker = before.side_to_move();
  return {before, attacker, style_attack_state(before, attacker, true), game_phase(before),
          undeveloped_minor_count(before, attacker), before.find_king(attacker)};
}

int style_score_from_metadata(const RootStyleContext& context, const Move& move,
                              const Board& after, const StyleAttackState& after_attack,
                              bool check, int escape_delta, bool sacrifice_candidate) noexcept {
  const Board& before = context.before;
  const StyleAttackState& before_attack = context.before_attack;
  const int reward = attack_reward(before_attack, after_attack, check);
  const int congregation = std::clamp(after_attack.congregation - before_attack.congregation, 0, 4);
  const int escapes = std::clamp(-escape_delta, 0, 3);
  int style = reward + congregation * 2 + escapes * 2 + (move.is_promotion() ? 8 : 0);
  const Piece moving = before.piece_at(move.from);
  const bool opening = context.phase >= 18;
  if (opening && is_quiet_move(move) && moving.type == PieceType::Pawn &&
      (move.from.file() == 0 || move.from.file() == 7)) style -= 6;
  if (opening && is_quiet_move(move) && moving.type == PieceType::Pawn &&
      (move.from.file() == 5 || move.from.file() == 6) && context.own_king.is_valid() &&
      context.own_king.file() >= 4 && after_attack.ring_control <= before_attack.ring_control &&
      after_attack.checking_motifs <= before_attack.checking_motifs) style -= 6;
  if (is_quiet_move(move) && moving.type != PieceType::Pawn) {
    const int ring_delta = after_attack.ring_control - before_attack.ring_control;
    const int participant_delta = after_attack.ring_participants - before_attack.ring_participants;
    const int congregation_delta = after_attack.congregation - before_attack.congregation;
    const int motif_delta = after_attack.checking_motifs - before_attack.checking_motifs;
    const int line_delta = after_attack.open_lines - before_attack.open_lines;
    const int threat_delta = after_attack.threats - before_attack.threats;
    int preparation = (ring_delta > 0) + (participant_delta > 0) + (congregation_delta > 0) +
                      (motif_delta > 0) + (line_delta > 0) + (threat_delta > 0);
    int bonus = preparation >= 2 ? std::min(10, preparation * 3) : 0;
    if (moving.type == PieceType::Queen) {
      if (context.undeveloped_minors >= 2) {
        const int concrete = (ring_delta >= 2) + (participant_delta >= 1) +
            (line_delta >= 1) + (threat_delta >= 4) + (motif_delta >= 2);
        if (concrete < 2) bonus = std::min(bonus, 3);
        style -= std::min(12, context.undeveloped_minors * 3);
      }
      if (preparation < 2) style -= 4;
    }
    style += bonus;
  }
  if (sacrifice_candidate) style += check ? 8 : 5;
  return style;
}

void populate_root_style_metadata(RootStyleContext& context, RootMoveInfo& info,
                                  const Board& after, bool use_style_v3,
                                  bool calculate_sacrifice_motifs) {
  const Board& before = context.before;
  const Color attacker = context.attacker;
  Board check_probe = before;
  const bool check = gives_check(check_probe, info.move);
  StyleAttackState after_attack = style_attack_state(after, attacker);
  const int escape_delta = count_king_escapes(after, opposite(attacker)) -
                           count_king_escapes(before, opposite(attacker));
  populate_attack_breakdown(info, context.before_attack, after_attack, check, escape_delta);
  info.sacrifice_kind = classify_sacrifice(before, info.move, after, context.before_attack,
                                           after_attack, check, escape_delta);
  info.sacrifice_candidate = info.sacrifice_kind != SacrificeKind::None;
  info.style_score = style_score_from_metadata(context, info.move, after, after_attack,
                                                check, escape_delta, info.sacrifice_candidate);
  info.king_break = after_attack.king_shield < context.before_attack.king_shield ||
                    after_attack.open_lines > context.before_attack.open_lines;
  info.style_tolerance = AGGRESSION_TOLERANCE_CP;
  if (use_style_v3 && calculate_sacrifice_motifs && is_quiet_move(info.move)) {
    if (context.before_sacrifice_motifs < 0)
      context.before_sacrifice_motifs = count_sacrifice_motifs(before, attacker);
    after_attack.sacrifice_motifs = count_sacrifice_motifs(after, attacker);
    info.sacrifice_motif_delta = after_attack.sacrifice_motifs - context.before_sacrifice_motifs;
    info.sacrifice_preparation = after_attack.sacrifice_motifs > context.before_sacrifice_motifs;
  }
  if (!use_style_v3) return;
  if (info.sacrifice_preparation) {
    info.style_tolerance = std::max(info.style_tolerance, 45);
    const int bonus = std::min(28, info.sacrifice_motif_delta * 14);
    info.style_score += bonus;
    info.concrete_attack_bonus += bonus;
  }
  if (info.king_break) {
    info.style_tolerance = std::max(info.style_tolerance, 50);
    const int shield_break = std::max(0, context.before_attack.king_shield - after_attack.king_shield);
    const int new_line = std::max(0, after_attack.open_lines - context.before_attack.open_lines);
    const int bonus = std::min(42, shield_break * 22 + new_line * 12 +
                               (shield_break > 0 && new_line > 0 ? 8 : 0));
    info.style_score += bonus;
    info.concrete_attack_bonus += bonus;
  }
  if (info.sacrifice_candidate) {
    info.style_tolerance = std::max(info.style_tolerance, 50);
    const bool negative_see = is_capture(info.move) && static_exchange_eval(before, info.move) < 0;
    const int bonus = (negative_see && check) ? 26 :
                      (negative_see && info.king_break) ? 24 : (check ? 14 : 9);
    info.style_score += bonus;
    info.concrete_attack_bonus += bonus;
  }
  if (info.sacrifice_kind == SacrificeKind::ExchangeSacrifice) {
    info.style_tolerance = std::max(info.style_tolerance, 50);
    // A losing RxN/RxB is difficult to misclassify: require both negative SEE
    // and independent king-attack signals above.  Give it enough weight to
    // compete with an otherwise quiet objective move inside the hard 50cp cap.
    info.style_score += 34;
    info.concrete_attack_bonus += 34;
  }
  info.style_tolerance = std::min(50, info.style_tolerance);
}

}  // namespace

int evaluate_move_style_from_analysis(const Board& before, const Move& move,
                                     const Board& after, const StyleAttackState& before_attack,
                                     const StyleAttackState& after_attack, bool check,
                                     int escape_delta, bool sacrifice_candidate) noexcept {
  const Color mover = before.side_to_move();
  const int reward = attack_reward(before_attack, after_attack, check);
  const int congregation = std::clamp(after_attack.congregation - before_attack.congregation, 0, 4);
  const int escapes = std::clamp(-escape_delta, 0, 3);
  int style = reward + congregation * 2 + escapes * 2 + (move.is_promotion() ? 8 : 0);
  const Piece moving = before.piece_at(move.from);
  const bool opening = game_phase(before) >= 18;
  const bool quiet_flank_pawn = opening && is_quiet_move(move) &&
                                moving.type == PieceType::Pawn &&
                                (move.from.file() == 0 || move.from.file() == 7);
  if (quiet_flank_pawn) style -= 6;
  if (opening && is_quiet_move(move) && moving.type == PieceType::Pawn &&
      (move.from.file() == 5 || move.from.file() == 6) &&
      before.find_king(mover).is_valid() && before.find_king(mover).file() >= 4 &&
      after_attack.ring_control <= before_attack.ring_control &&
      after_attack.checking_motifs <= before_attack.checking_motifs) {
    style -= 6;
  }
  // Quiet preparation receives its reward only when several independent
  // attacking signals improve together; a lone queen sortie cannot dominate.
  if (is_quiet_move(move) && moving.type != PieceType::Pawn) {
    const int ring_delta = after_attack.ring_control - before_attack.ring_control;
    const int participant_delta = after_attack.ring_participants - before_attack.ring_participants;
    const int congregation_delta = after_attack.congregation - before_attack.congregation;
    const int motif_delta = after_attack.checking_motifs - before_attack.checking_motifs;
    const int line_delta = after_attack.open_lines - before_attack.open_lines;
    const int threat_delta = after_attack.threats - before_attack.threats;
    int preparation = 0;
    preparation += ring_delta > 0;
    preparation += participant_delta > 0;
    preparation += congregation_delta > 0;
    preparation += motif_delta > 0;
    preparation += line_delta > 0;
    preparation += threat_delta > 0;
    int preparation_bonus = preparation >= 2 ? std::min(10, preparation * 3) : 0;
    if (moving.type == PieceType::Queen) {
      const int development_debt = opening ? undeveloped_minor_count(before, mover) : 0;
      if (development_debt >= 2) {
        // Before development is complete, ring control plus a fresh checking
        // motif alone is too easy for a queen to manufacture.  Preserve the
        // full reward only when multiple concrete attacking gains accompany it.
        int concrete_attack_signals = 0;
        concrete_attack_signals += ring_delta >= 2;
        concrete_attack_signals += participant_delta >= 1;
        concrete_attack_signals += line_delta >= 1;
        concrete_attack_signals += threat_delta >= 4;
        concrete_attack_signals += motif_delta >= 2;
        if (concrete_attack_signals < 2) preparation_bonus = std::min(preparation_bonus, 3);
        style -= std::min(12, development_debt * 3);
      }
      if (preparation < 2) style -= 4;
    }
    style += preparation_bonus;
  }
  if (sacrifice_candidate) style += check ? 8 : 5;
  return style;
}

int evaluate_move_style(const Board& before, const Move& move,
                        const Board& after) noexcept {
  const Color mover = before.side_to_move();
  Board probe = before;
  const bool check = gives_check(probe, move);
  const StyleAttackState before_attack = style_attack_state(before, mover, true);
  const StyleAttackState after_attack = style_attack_state(after, mover);
  const int escape_delta = count_king_escapes(after, opposite(mover)) -
                           count_king_escapes(before, opposite(mover));
  const bool sacrifice = classify_sacrifice(before, move, after, before_attack,
                                             after_attack, check, escape_delta) != SacrificeKind::None;
  return evaluate_move_style_from_analysis(before, move, after, before_attack, after_attack,
                                           check, escape_delta, sacrifice);
}

bool is_sacrifice_candidate(const Board& before, const Move& move,
                            const Board& after) noexcept {
  const Color mover = before.side_to_move();
  Board probe = before;
  const bool check = gives_check(probe, move);
  const StyleAttackState before_attack = style_attack_state(before, mover);
  const StyleAttackState after_attack = style_attack_state(after, mover);
  const int escape_delta = count_king_escapes(after, opposite(mover)) -
                           count_king_escapes(before, opposite(mover));
  return classify_sacrifice(before, move, after, before_attack, after_attack,
                            check, escape_delta) != SacrificeKind::None;
}

bool is_style_score_safe(int objective_score, int candidate_score,
                         int tolerance) noexcept {
  return candidate_score >= objective_score - std::clamp(tolerance,
                                                          AGGRESSION_TOLERANCE_CP, 50);
}

int quiescence_impl(Board& board, int alpha, int beta, int ply,
                    SearchContext& context) {
  if (context.should_stop()) return 0;
  auto& nodes = context.nodes;
  auto& qnodes = context.qnodes;
  ++nodes;
  ++qnodes;
  const Color side = board.side_to_move();
  const Square king = board.find_king(side);
  const bool in_check = king.is_valid() &&
                        board.is_square_attacked(king, opposite(side));
  const std::vector<Move> legal = in_check ? generate_legal_moves(board) : std::vector<Move>{};

  if (in_check) {
    if (legal.empty()) return -MATE_SCORE + ply;
    const auto evasions = order_moves(board, legal, context.result, context.heuristics, ply,
                                      std::nullopt, true);
    int best = -MATE_SCORE;
    for (const OrderedMove& item : evasions) {
      const UndoState undo = board.make_move(item.move);
      const int score = -quiescence_impl(board, -beta, -alpha, ply + 1,
                                         context);
      board.unmake_move(item.move, undo);
      if (context.stopped) return 0;
      best = std::max(best, score);
      alpha = std::max(alpha, score);
      if (alpha >= beta) break;
    }
    return best;
  }

  const int stand_pat = evaluate(board, context.eval_mode).value_or(evaluate_hce(board));
  if (stand_pat >= beta) return beta;
  alpha = std::max(alpha, stand_pat);

  const std::vector<Move> tactical_moves = generate_legal_tactical_moves(board);
  if (tactical_moves.empty() && generate_legal_moves(board).empty()) return 0;
  const auto tactical = order_moves(board, tactical_moves, context.result, nullptr, ply,
                                    std::nullopt, true);
  for (const OrderedMove& item : tactical) {
    const Move& move = item.move;
    const bool promotion = move.is_promotion();
    const bool capture = item.capture;
    const bool check = item.gives_check;
    const int see = item.see;
    if (context.use_see_pruning && capture && see < -100 && !check && !promotion) {
      if (context.result != nullptr) ++context.result->see_prunes;
      continue;
    }
    // A non-checking, non-promotion capture cannot raise alpha when even the
    // captured material plus a deliberately generous margin is insufficient.
    // Keep checks and promotions out: their tactical value is not bounded by
    // the immediate victim value.
    const Piece victim = move.flag == MoveFlag::EnPassant
        ? Piece{PieceType::Pawn, opposite(side)} : board.piece_at(move.to);
    constexpr int DELTA_MARGIN_CP = 120;
    if (capture && !check && !promotion &&
        stand_pat + piece_value(victim.type) + DELTA_MARGIN_CP < alpha) {
      if (context.result != nullptr) ++context.result->qdelta_prunes;
      continue;
    }
    const UndoState undo = board.make_move(move);
    const int score = -quiescence_impl(board, -beta, -alpha, ply + 1,
                                       context);
    board.unmake_move(move, undo);
    if (context.stopped) return 0;
    alpha = std::max(alpha, score);
    if (alpha >= beta) break;
  }
  return alpha;
}

int negamax_impl(Board& board, int depth, int alpha, int beta, int ply,
                 SearchContext& context, bool was_null_move = false) {
  if (context.should_stop()) return 0;
  if (depth <= 0) return quiescence_impl(board, alpha, beta, ply, context);
  const int original_alpha = alpha;
  const int original_beta = beta;
  std::optional<Move> tt_move;
  if (context.tt != nullptr && context.result != nullptr) {
    ++context.result->tt_probes;
    const TTEntry* entry = context.tt->probe(board.zobrist_key());
    if (entry != nullptr) {
      ++context.result->tt_hits;
      tt_move = entry->best_move;
      if (entry->depth >= depth) {
        const int score = score_from_tt(entry->score, ply);
        if (entry->bound == TTBound::Exact ||
            (entry->bound == TTBound::Lower && score >= beta) ||
            (entry->bound == TTBound::Upper && score <= alpha)) {
          ++context.result->tt_cutoffs;
          return score;
        }
        if (entry->bound == TTBound::Lower) alpha = std::max(alpha, score);
        if (entry->bound == TTBound::Upper) beta = std::min(beta, score);
        if (alpha >= beta) {
          ++context.result->tt_cutoffs;
          return score;
        }
      }
    }
  }
  ++context.nodes;
  const Square king = board.find_king(board.side_to_move());
  const bool in_check = king.is_valid() &&
                        board.is_square_attacked(king, opposite(board.side_to_move()));
  if (context.use_null_move && depth >= 3 && ply > 0 && !was_null_move &&
      !in_check && has_non_pawn_material(board, board.side_to_move())) {
    if (context.result != nullptr) ++context.result->null_attempts;
    const int reduction = depth <= 5 ? 2 : 3;
    const NullUndoState undo = board.make_null_move();
    const int score = -negamax_impl(board, depth - 1 - reduction,
                                    -beta, -beta + 1, ply + 1, context, true);
    board.unmake_null_move(undo);
    if (context.stopped) return 0;
    if (score >= beta) {
      if (context.result != nullptr) ++context.result->null_cutoffs;
      return score;
    }
  }
  std::vector<Move> moves = generate_legal_moves(board);
  if (moves.empty()) {
    const Square king = board.find_king(board.side_to_move());
    return king.is_valid() && board.is_square_attacked(king, opposite(board.side_to_move()))
               ? -MATE_SCORE + ply : 0;
  }
  int best = -MATE_SCORE;
  std::optional<Move> best_move;
  const auto ordered_moves = order_moves(board, moves, context.result, context.heuristics,
                                         ply, tt_move);
  for (std::size_t move_index = 0; move_index < ordered_moves.size(); ++move_index) {
    const OrderedMove& ordered = ordered_moves[move_index];
    const Move& move = ordered.move;
    const bool killer_move = context.heuristics != nullptr && ply >= 0 &&
        ply < MAX_SEARCH_PLY && (move == context.heuristics->killers[ply][0] ||
                                 move == context.heuristics->killers[ply][1]);
    const bool high_history = context.heuristics != nullptr &&
        context.heuristics->history_score(board.side_to_move(), move) >= 1000;
    const bool lmr_candidate = context.use_lmr && depth >= 3 && move_index >= 4 &&
        !in_check && is_quiet_move(move) && !killer_move && !high_history &&
        !gives_check(board, move);
    const bool pvs_candidate = context.use_pvs && move_index > 0;
    const UndoState undo = board.make_move(move);
    int score = 0;
    const auto search_child = [&](int child_depth, bool zero_window) {
      if (zero_window && context.result != nullptr)
        ++context.result->pvs_zero_window_searches;
      if (zero_window)
        return -negamax_impl(board, child_depth, -alpha - 1, -alpha,
                             ply + 1, context);
      return -negamax_impl(board, child_depth, -beta, -alpha, ply + 1,
                           context);
    };
    if (lmr_candidate) {
      if (context.result != nullptr) ++context.result->lmr_attempts;
      const std::uint64_t before_reduced = context.nodes;
      score = search_child(depth - 2, pvs_candidate);
      if (context.result != nullptr)
        context.result->lmr_reduced_search_nodes += context.nodes - before_reduced;
      // In a null window, equality is a fail-low and cannot improve alpha.
      // Re-searching it was the main source of the inflated LMR rate.
      if (!context.stopped && score > alpha) {
        if (context.result != nullptr) ++context.result->lmr_researches;
        const std::uint64_t before_research = context.nodes;
        score = search_child(depth - 1, pvs_candidate);
        if (context.result != nullptr)
          context.result->lmr_research_nodes += context.nodes - before_research;
        if (!context.stopped && pvs_candidate && score > alpha && score < beta) {
          if (context.result != nullptr) ++context.result->pvs_researches;
          const std::uint64_t before_pvs_research = context.nodes;
          score = search_child(depth - 1, false);
          if (context.result != nullptr)
            context.result->pvs_research_nodes += context.nodes - before_pvs_research;
        }
      }
    } else {
      score = search_child(depth - 1, pvs_candidate);
      if (!context.stopped && pvs_candidate && score > alpha && score < beta) {
        if (context.result != nullptr) ++context.result->pvs_researches;
        const std::uint64_t before_pvs_research = context.nodes;
        score = search_child(depth - 1, false);
        if (context.result != nullptr)
          context.result->pvs_research_nodes += context.nodes - before_pvs_research;
      }
    }
    board.unmake_move(move, undo);
    if (context.stopped) return 0;
    if (score > best) {
      best = score;
      best_move = move;
    }
    alpha = std::max(alpha, score);
    if (alpha >= beta) {
      if (context.heuristics != nullptr && is_quiet_move(move)) {
        const bool was_killer = ply >= 0 && ply < MAX_SEARCH_PLY &&
            (context.heuristics->killers[ply][0] == move ||
             context.heuristics->killers[ply][1] == move);
        context.heuristics->store_killer(move, ply);
        context.heuristics->update_history(board.side_to_move(), move, depth);
        if (context.result != nullptr) {
          ++context.result->history_cutoffs;
          if (was_killer) ++context.result->killer_cutoffs;
        }
      }
      break;
    }
  }
  if (context.tt != nullptr && context.result != nullptr) {
    const TTBound bound = best <= original_alpha ? TTBound::Upper
                         : best >= original_beta ? TTBound::Lower : TTBound::Exact;
    context.tt->store(board.zobrist_key(), depth, score_to_tt(best, ply), bound,
                      best_move);
  }
  return best;
}

int negamax(Board& board, int depth, int alpha, int beta, int ply,
            std::uint64_t& nodes) {
  std::uint64_t qnodes = 0;
  SearchContext context{nodes, qnodes};
  return negamax_impl(board, depth, alpha, beta, ply, context);
}

int quiescence(Board& board, int alpha, int beta, int ply) {
  std::uint64_t nodes = 0;
  std::uint64_t qnodes = 0;
  SearchContext context{nodes, qnodes};
  return quiescence_impl(board, alpha, beta, ply, context);
}

SearchResult search(const Board& position, int max_depth) {
  SearchLimits limits;
  limits.max_depth = max_depth;
  return search(position, limits);
}

SearchResult search(const Board& position, const SearchLimits& limits,
                    const SearchInfoCallback& on_iteration) {
  SearchResult result;
  const auto search_started = std::chrono::steady_clock::now();
  if (limits.max_depth < 1) return result;
  Board root = position;
  TranspositionTable& tt = transposition_table();
  std::vector<Move> legal = generate_legal_moves(root);
  if (legal.empty()) {
    SearchContext context{result.nodes, result.qnodes, limits.has_deadline,
                          limits.deadline, false,
                          limits.use_tt ? &tt : nullptr, &result,
                          limits.use_see_pruning,
                          limits.use_killer_history ? &search_heuristics() : nullptr,
                          limits.use_null_move, limits.use_lmr, limits.use_pvs, limits.eval_mode,
                          limits.deadline_check_interval_nodes};
    result.score = negamax_impl(root, 0, -MATE_SCORE, MATE_SCORE, 0, context);
    return result;
  }
  result.best_move = legal.front();
  // Reserve scheduling deliberately uses only move fields and king proximity.
  // Full attack/motif analysis belongs after the objective root scores exist.
  const Color root_mover = root.side_to_move();
  const Square enemy_king = root.find_king(opposite(root_mover));
  const bool has_concrete_style_candidate = limits.use_style_v3 && std::any_of(
      legal.begin(), legal.end(), [&](const Move& move) {
        const Piece moving = root.piece_at(move.from);
        const Piece captured = root.piece_at(move.to);
        const int proximity = enemy_king.is_valid() ?
            chebyshev_distance(move.to, enemy_king) : 8;
        return is_capture(move) || move.is_promotion() ||
               (moving.type != PieceType::Pawn && moving.type != PieceType::King &&
                (proximity <= 3 || !captured.is_empty()));
      });
  const int requested_reserve = limits.style_verification_reserve_ms >= 0
      ? limits.style_verification_reserve_ms : 150;
  // Do not take a large objective-search slice in ordinary positions.  A
  // small guard still prevents a deadline edge from exposing partial work.
  const int reserve_ms = limits.has_deadline
      ? (has_concrete_style_candidate ? requested_reserve : 10) : 0;
  result.style_verification_reserve_active = limits.has_deadline &&
      has_concrete_style_candidate && reserve_ms > 0;
  result.style_verification_reserve_ms = static_cast<std::uint64_t>(reserve_ms);
  const auto objective_deadline = limits.has_deadline
      ? limits.deadline - std::chrono::milliseconds(reserve_ms) : limits.deadline;
  std::vector<RootMoveInfo> previous_root;
  std::vector<RootMoveInfo> last_completed;
  int last_objective_best = -MATE_SCORE;
  for (int depth = 1; depth <= limits.max_depth; ++depth) {
    const int previous_score = result.score;
    const bool mate_score = previous_score > MATE_SCORE - 1000 ||
                            previous_score < -MATE_SCORE + 1000;
    const bool use_aspiration = limits.use_aspiration && depth > 1 && !mate_score;
    int window = ASPIRATION_INITIAL_WINDOW_CP;
    int alpha = use_aspiration ? std::max(-MATE_SCORE, previous_score - window)
                                : -MATE_SCORE;
    int beta = use_aspiration ? std::min(MATE_SCORE, previous_score + window)
                              : MATE_SCORE;
    std::vector<RootMoveInfo> current;
    int best_score = -MATE_SCORE;
    bool completed = false;
    bool stopped_iteration = false;
    while (!completed) {
      SearchContext context{result.nodes, result.qnodes, limits.has_deadline,
                            objective_deadline, false,
                            limits.use_tt ? &tt : nullptr, &result,
                            limits.use_see_pruning,
                            limits.use_killer_history ? &search_heuristics() : nullptr,
                            limits.use_null_move, limits.use_lmr, limits.use_pvs, limits.eval_mode,
                            limits.deadline_check_interval_nodes};
      current.clear();
      best_score = -MATE_SCORE;
      std::optional<Move> root_tt_move;
      if (limits.use_tt) {
        if (const TTEntry* entry = tt.probe(root.zobrist_key()); entry != nullptr)
          root_tt_move = entry->best_move;
      }
      auto root_order = order_moves(root, legal, &result,
                                    limits.use_killer_history ? &search_heuristics() : nullptr,
                                    0, root_tt_move);
      for (OrderedMove& item : root_order) {
        if (item.move == result.best_move) item.score += 10000000;
        const auto previous = std::find_if(previous_root.begin(), previous_root.end(),
            [&item](const RootMoveInfo& info) { return info.move == item.move; });
        if (previous != previous_root.end()) item.score += 6000000 + previous->search_score;
      }
      std::sort(root_order.begin(), root_order.end(), [](const OrderedMove& a, const OrderedMove& b) {
        return a.score != b.score ? a.score > b.score : move_order_less(a.move, b.move);
      });
      for (std::size_t move_index = 0; move_index < root_order.size(); ++move_index) {
        const Move& move = root_order[move_index].move;
        if (context.should_stop()) break;
        const UndoState undo = root.make_move(move);
        int score = 0;
        const bool zero_window = limits.use_pvs && move_index > 0;
        ScoreBound bound = ScoreBound::Exact;
        if (zero_window) {
          ++result.pvs_zero_window_searches;
          score = -negamax_impl(root, depth - 1, -alpha - 1, -alpha, 1, context);
          if (!context.stopped && score > alpha && score < beta) {
            ++result.pvs_researches;
            score = -negamax_impl(root, depth - 1, -beta, -alpha, 1, context);
          } else if (score <= alpha) {
            bound = ScoreBound::Upper;
          } else {
            bound = ScoreBound::Lower;
          }
        } else {
          score = -negamax_impl(root, depth - 1, -beta, -alpha, 1, context);
          if (score >= beta) bound = ScoreBound::Lower;
        }
        root.unmake_move(move, undo);
        if (context.stopped) break;
        current.push_back({move, score, bound});
        best_score = std::max(best_score, score);
        alpha = std::max(alpha, score);
      }
      if (context.stopped || current.size() != legal.size()) {
        stopped_iteration = true;
        break;
      }
      // Root alpha is updated while searching, so retain the original window
      // edge for reliable aspiration classification.
      const bool outside_low = use_aspiration && best_score <=
                               (previous_score - window);
      const bool outside_high = use_aspiration && best_score >= beta;
      if (outside_low || outside_high) {
        if (outside_low) ++result.aspiration_fail_lows;
        if (outside_high) ++result.aspiration_fail_highs;
        ++result.aspiration_retries;
        if (window >= MATE_SCORE) {
          alpha = -MATE_SCORE;
          beta = MATE_SCORE;
          completed = true;
        } else {
          window = std::min(MATE_SCORE, window * 2);
          alpha = std::max(-MATE_SCORE, previous_score - window);
          beta = std::min(MATE_SCORE, previous_score + window);
        }
        continue;
      }
      completed = true;
    }
    if (stopped_iteration || current.size() != legal.size()) break;
    int objective_best = -MATE_SCORE;
    auto objective_move = current.end();
    for (auto it = current.begin(); it != current.end(); ++it) {
      if (it->bound == ScoreBound::Exact &&
          (objective_move == current.end() || it->search_score > objective_best)) {
        objective_best = it->search_score;
        objective_move = it;
      }
    }
    if (objective_move == current.end()) break;
    const Move objective_best_move = objective_move->move;
    last_completed = std::move(current);
    last_objective_best = objective_best;
    result.best_move = objective_best_move;
    result.score = objective_best;
    result.completed_depth = depth;
    result.root_moves = last_completed;
    if (on_iteration) on_iteration(depth, result.score, result.nodes, result.qnodes);
    previous_root = last_completed;
    legal = generate_legal_moves(root);
  }
  if (last_completed.empty()) {
    result.objective_time_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - search_started).count());
    result.main_nodes = result.nodes - result.qnodes;
    return result;
  }
  result.objective_time_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - search_started).count());
  auto& current = last_completed;
  const int depth = result.completed_depth;
  const int objective_best = last_objective_best;
  // A move farther than the hard 50cp cap can never be selected by v3.2.
  // Analyse only final exact eligible root moves, sharing immutable before
  // state and a single child board per candidate.
  const auto metadata_started = std::chrono::steady_clock::now();
  StyleProfile profile{result};
  StyleProfile* const previous_profile = active_style_profile;
  if (limits.profile_style_metadata) active_style_profile = &profile;
  RootStyleContext style_context = make_root_style_context(position);
  for (RootMoveInfo& info : current) {
    const int loss = objective_best - info.search_score;
    const int maximum_loss = limits.use_style_v3 ? 50 : AGGRESSION_TOLERANCE_CP;
    const Piece moving = root.piece_at(info.move.from);
    const int proximity = enemy_king.is_valid() ? chebyshev_distance(info.move.to, enemy_king) : 8;
    const bool cheap_concrete_move = is_capture(info.move) || info.move.is_promotion() ||
        moving.type == PieceType::Queen ||
        (moving.type != PieceType::Pawn && moving.type != PieceType::King && proximity <= 3);
    // Preserve diagnostics for obviously concrete captures even when an
    // objective loss makes them ineligible for selection.  Quiet motif work
    // remains strictly capped by the 50cp selection bound.
    if ((info.bound != ScoreBound::Exact || loss > maximum_loss) &&
        !(limits.use_style_v3 && cheap_concrete_move)) continue;
    Board child = root;
    if (active_style_profile != nullptr) ++active_style_profile->result.style_child_boards;
    child.make_move(info.move);
    populate_root_style_metadata(style_context, info, child, limits.use_style_v3,
                                 limits.use_style_v3 && loss <= 50);
    ++result.style_evaluations;
  }
  active_style_profile = previous_profile;
  result.root_style_metadata_time_us += static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - metadata_started).count());
  const Square root_king = root.find_king(root.side_to_move());
  const bool root_in_check = root_king.is_valid() &&
      root.is_square_attacked(root_king, opposite(root.side_to_move()));
  const auto is_capture_evasion = [&](const RootMoveInfo& info) {
      if (!root_in_check || !is_capture(info.move)) return false;
      // Preserve the ordinary objective choice for a pawn-check capture;
      // this exception is for clear high-value forced recaptures.
      if (piece_value(root.piece_at(info.move.to).type) < piece_value(PieceType::Queen))
        return false;
      Board evasion = root;
      const UndoState undo = evasion.make_move(info.move);
      const Square king = evasion.find_king(root.side_to_move());
      const bool safe = king.is_valid() &&
          !evasion.is_square_attacked(king, opposite(root.side_to_move()));
      evasion.unmake_move(info.move, undo);
      return safe;
  };
  auto objective_move = std::max_element(current.begin(), current.end(), [](const RootMoveInfo& a, const RootMoveInfo& b) {
      if (a.bound != ScoreBound::Exact) return true;
      if (b.bound != ScoreBound::Exact) return false;
      return a.search_score < b.search_score;
  });
  for (RootMoveInfo& info : current) {
    if (info.bound == ScoreBound::Exact) {
      info.style_safe = is_style_score_safe(objective_best, info.search_score,
                                             info.style_tolerance);
      if (info.style_safe) ++result.root_style_verified;
      else ++result.root_style_rejected;
    }
  }
  RootMoveInfo* chosen = &*objective_move;
  const bool mate_found = objective_best > MATE_SCORE - 1000 || objective_best < -MATE_SCORE + 1000;
  if (!mate_found) {
    for (RootMoveInfo& candidate : current) {
      if (!candidate.style_safe || &candidate == chosen) continue;
      if (candidate.style_score > chosen->style_score ||
          (candidate.style_score == chosen->style_score &&
           (candidate.search_score > chosen->search_score ||
            (candidate.search_score == chosen->search_score &&
             (candidate.move.from.index() < chosen->move.from.index() ||
              (candidate.move.from == chosen->move.from &&
               candidate.move.to.index() < chosen->move.to.index())))))) {
        chosen = &candidate;
      }
    }
  }
  std::vector<RootMoveInfo*> candidates;
  for (RootMoveInfo& info : current) {
    if (&info == &*objective_move || info.bound == ScoreBound::Exact) continue;
    ++result.root_style_candidates;
    if (limits.use_style_v3 &&
        !(info.sacrifice_candidate || info.king_break || info.sacrifice_preparation ||
          info.style_tolerance > AGGRESSION_TOLERANCE_CP)) {
      ++result.root_style_prefilter_skips;
      continue;
    }
    const bool forced_evasion = is_capture_evasion(info);
    if (!forced_evasion && (info.style_score < objective_move->style_score ||
        (info.style_score == objective_move->style_score &&
         !(info.search_score > objective_best ||
           (info.search_score == objective_best &&
            (info.move.from.index() < objective_move->move.from.index() ||
             (info.move.from == objective_move->move.from && info.move.to.index() < objective_move->move.to.index()))))))) {
      ++result.root_style_prefilter_skips;
      continue;
    }
    if (info.bound == ScoreBound::Upper &&
        !is_style_score_safe(objective_best, info.search_score, info.style_tolerance)) {
      ++result.root_style_rejected;
      continue;
    }
    candidates.push_back(&info);
  }
  std::sort(candidates.begin(), candidates.end(), [](const RootMoveInfo* a, const RootMoveInfo* b) {
    return a->style_score != b->style_score
               ? a->style_score > b->style_score
               : move_order_less(a->move, b->move);
  });
  constexpr std::size_t kMaxStyleVerificationCandidates = 4;
  if (candidates.size() > kMaxStyleVerificationCandidates)
    candidates.resize(kMaxStyleVerificationCandidates);
  result.root_style_shortlist = candidates.size();
  const auto verification_started = std::chrono::steady_clock::now();
  SearchContext verification{result.nodes, result.qnodes, limits.has_deadline,
                             limits.deadline, false, limits.use_tt ? &tt : nullptr, &result,
                             limits.use_see_pruning,
                             limits.use_killer_history ? &search_heuristics() : nullptr,
                             limits.use_null_move, limits.use_lmr, limits.use_pvs,
                             limits.eval_mode, 1};
  result.style_verification_eval_mode = verification.eval_mode;
  for (RootMoveInfo* candidate : candidates) {
    if (candidate->style_score < chosen->style_score) {
      ++result.root_style_prefilter_skips;
      continue;
    }
    ++result.root_style_verification_searches;
    const std::uint64_t before_verification = result.nodes;
    const auto candidate_started = std::chrono::steady_clock::now();
    const UndoState undo = root.make_move(candidate->move);
    const int threshold = objective_best - candidate->style_tolerance;
    const int proof = -negamax_impl(root, depth - 1, -threshold, -threshold + 1, 1, verification);
    root.unmake_move(candidate->move, undo);
    const auto candidate_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - candidate_started).count());
    result.style_verification_max_ms = std::max(result.style_verification_max_ms, candidate_ms);
    result.root_style_verification_nodes += result.nodes - before_verification;
    if (verification.stopped) {
      ++result.root_style_verification_timeouts;
      break;
    }
    candidate->style_safe = proof >= threshold;
    if (candidate->style_safe) {
      ++result.root_style_verified;
      ++result.root_style_verification_proven;
      if (candidate->style_score > chosen->style_score ||
          (candidate->style_score == chosen->style_score &&
           (candidate->search_score > chosen->search_score ||
            (candidate->search_score == chosen->search_score &&
             (candidate->move.from.index() < chosen->move.from.index() ||
              (candidate->move.from == chosen->move.from &&
               candidate->move.to.index() < chosen->move.to.index())))))) {
        chosen = candidate;
      }
    } else ++result.root_style_rejected;
  }
  result.style_verification_time_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - verification_started).count());
  if (!verification.stopped && !mate_found) {
    for (RootMoveInfo& info : current) if (info.style_safe)
      info.see_score = move_see(position, info.move, &result);
    if (root_in_check) {
      for (RootMoveInfo& info : current) {
        if (info.style_safe && is_capture_evasion(info)) {
          chosen = &info;
          break;
        }
      }
    }
  }
  // `chosen` begins at the completed objective best and can only be replaced
  // by an exact completed score or a finished threshold proof.  Thus a timed
  // proof may use the safe completed subset without ever treating an
  // unfinished candidate as style-safe.
  result.best_move = chosen->move;
  result.score = objective_best;
  result.root_moves = std::move(current);
  result.main_nodes = result.nodes - result.qnodes;
  return result;
}

void clear_transposition_table() noexcept { transposition_table().clear(); }

void clear_search_heuristics() noexcept { search_heuristics().clear(); }

}  // namespace hebichess
