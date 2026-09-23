#include "chess/search.hpp"
#include "chess/time_management.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <optional>
#include <unordered_map>

#include "chess/nnue.hpp"
#include "chess/see.hpp"
#include "chess/uci.hpp"

namespace hebichess {
namespace {

#ifndef HEBICHESS_QSEARCH_TT_VARIANT
#define HEBICHESS_QSEARCH_TT_VARIANT 0
#endif

#ifndef HEBICHESS_QSEARCH_TT_PROFILE
#define HEBICHESS_QSEARCH_TT_PROFILE 0
#endif

#ifndef HEBICHESS_QSEARCH_DELTA_PRUNING
#define HEBICHESS_QSEARCH_DELTA_PRUNING 1
#endif

#ifndef HEBICHESS_QSEARCH_LAZY_CHECKS
#define HEBICHESS_QSEARCH_LAZY_CHECKS 0
#endif

#ifndef HEBICHESS_QSEARCH_TT_CUTOFF_MASK
#if HEBICHESS_QSEARCH_TT_VARIANT == 2
#define HEBICHESS_QSEARCH_TT_CUTOFF_MASK 7
#else
#define HEBICHESS_QSEARCH_TT_CUTOFF_MASK 0
#endif
#endif

#if HEBICHESS_QSEARCH_TT_VARIANT < 0 || HEBICHESS_QSEARCH_TT_VARIANT > 2
#error "HEBICHESS_QSEARCH_TT_VARIANT must be 0 (baseline), 1 (shadow), or 2 (active)"
#endif

#if HEBICHESS_QSEARCH_TT_CUTOFF_MASK < 0 || HEBICHESS_QSEARCH_TT_CUTOFF_MASK > 7
#error "HEBICHESS_QSEARCH_TT_CUTOFF_MASK is a three-bit Exact/Lower/Upper mask"
#endif

TranspositionTable& transposition_table() {
  static TranspositionTable table(64);
  return table;
}

#if HEBICHESS_QSEARCH_TT_VARIANT != 0
// Keep depth-zero QSearch entries completely out of the main table. Current
// negamax routes depth zero directly to QSearch before its main-TT probe, but
// a separate table makes QSearch eligibility explicit and guarantees zero
// main-TT replacement pressure.
TranspositionTable& qsearch_transposition_table() {
  static TranspositionTable table(16);
  return table;
}

void clear_qsearch_transposition_table() noexcept {
  qsearch_transposition_table().clear();
}
#endif

constexpr int MAX_SEARCH_PLY = 128;
constexpr int MAX_HISTORY = 32768;
constexpr int ASPIRATION_INITIAL_WINDOW_CP = 35;
constexpr int QSEARCH_DELTA_MARGIN_CP = 120;

constexpr int QTT_EXACT_CUTOFF = 1;
constexpr int QTT_LOWER_CUTOFF = 2;
constexpr int QTT_UPPER_CUTOFF = 4;

#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
thread_local QsearchTtCutoffTraceCallback qsearch_tt_cutoff_trace_callback;

struct QsearchTtStoreProvenance {
  ZobristKey key{0};
  std::uint64_t serial{0};
  std::string fen{};
  int ply{0};
  int alpha{0};
  int beta{0};
  int result{0};
  TTBound bound{TTBound::Exact};
  int encoded_score{0};
  std::optional<float> raw_nnue{};
  std::uint64_t accumulator_checksum{0};
};

struct QsearchTtDiagnosticState {
  bool override_bounds{false};
  std::uint8_t bound_mask{0};
  std::int64_t cutoff_limit{-1};
  std::uint64_t trace_cutoff{0};
  std::uint64_t cutoff_serial{0};
  std::uint64_t store_serial{0};
  std::unordered_map<ZobristKey, QsearchTtStoreProvenance> stores{};
};

std::uint64_t accumulator_checksum(const NnueAccumulator* accumulator) noexcept {
  if (accumulator == nullptr) return 0;
  std::uint64_t hash = 1469598103934665603ULL;
  const auto mix = [&hash](float value) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    for (int shift = 0; shift < 32; shift += 8) {
      hash ^= (bits >> shift) & 0xffU;
      hash *= 1099511628211ULL;
    }
  };
  for (const float value : accumulator->white) mix(value);
  for (const float value : accumulator->black) mix(value);
  return hash;
}

std::optional<float> raw_nnue(const Board& board, const NnueAccumulator* accumulator) {
  return accumulator != nullptr ? evaluate_nnue_network_raw_from_accumulator(board, *accumulator)
                                : evaluate_nnue_network_raw(board);
}
#endif

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
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
  QsearchTtDiagnosticState* qtt_diagnostic{nullptr};
  // Used solely by the trace's QTT-off oracle re-searches.
  bool qtt_diagnostic_disable{false};
  std::vector<QsearchNodeTrace>* qsearch_window_trace{nullptr};
  bool qsearch_diagnostic_disable_delta_pruning{false};
#endif
#if defined(HEBICHESS_NNUE_SEARCH_TEST)
  // Test-only: false makes NNUE evaluations follow the old board rebuild
  // path, so the same search can be compared against the wired path.
  bool use_nnue_accumulator{true};
  NnueSearchAccumulatorCounters* nnue_counters{nullptr};
#endif

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

int evaluate_search_position(const Board& board, SearchContext& context,
                             const NnueAccumulator* accumulator,
                             bool qsearch) {
  if (qsearch && context.result != nullptr && context.eval_mode == EvalMode::NNUE &&
      nnue_network_available()) {
    ++context.result->q_nnue_evals;
  }
#if HEBICHESS_QSEARCH_TT_PROFILE
  if (context.result != nullptr) ++context.result->eval_calls;
#endif
  if (accumulator != nullptr) {
    const auto raw = evaluate_nnue_network_raw_from_accumulator(board, *accumulator);
    if (raw.has_value()) {
#if defined(HEBICHESS_NNUE_SEARCH_TEST)
      if (context.nnue_counters != nullptr) {
        ++context.nnue_counters->eval_from_accumulator_count;
        if (qsearch) ++context.nnue_counters->qsearch_eval_from_accumulator_count;
      }
#endif
      return static_cast<int>(std::lround(*raw));
    }
  }
#if defined(HEBICHESS_NNUE_SEARCH_TEST)
  if (context.nnue_counters != nullptr && context.eval_mode == EvalMode::NNUE &&
      nnue_network_available()) {
    ++context.nnue_counters->legacy_full_eval_count;
  }
#endif
  return evaluate(board, context.eval_mode).value_or(evaluate_hce(board));
}

const NnueAccumulator* make_child_accumulator(
    const Board& parent, const Move& move, const NnueAccumulator* parent_accumulator,
    NnueAccumulator& child_accumulator, SearchContext& context, bool qsearch) {
  if (parent_accumulator == nullptr ||
      !update_nnue_accumulator(parent, move, *parent_accumulator, child_accumulator)) {
    return nullptr;
  }
#if defined(HEBICHESS_NNUE_SEARCH_TEST)
  if (context.nnue_counters != nullptr) {
    ++context.nnue_counters->incremental_update_count;
    if (qsearch) ++context.nnue_counters->qsearch_incremental_update_count;
    if (parent.piece_at(move.from).type == PieceType::King)
      ++context.nnue_counters->king_perspective_refresh_count;
    if (move.flag == MoveFlag::Capture || move.flag == MoveFlag::PromotionCapture)
      ++context.nnue_counters->capture_incremental_update_count;
    if (move.flag == MoveFlag::CastleKingSide || move.flag == MoveFlag::CastleQueenSide)
      ++context.nnue_counters->castling_incremental_update_count;
    if (move.is_promotion()) ++context.nnue_counters->promotion_incremental_update_count;
    if (move.flag == MoveFlag::EnPassant)
      ++context.nnue_counters->en_passant_incremental_update_count;
  }
#endif
  if (qsearch && context.result != nullptr && context.eval_mode == EvalMode::NNUE &&
      nnue_network_available()) {
    ++context.result->q_nnue_incremental_updates;
  }
  return &child_accumulator;
}

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
                                     bool include_checks = false,
                                     bool qsearch = false,
                                     bool qsearch_evasion = false,
                                     const std::optional<int>& qsearch_stand_pat = std::nullopt,
                                     const std::optional<int>& qsearch_alpha = std::nullopt) {
  std::vector<OrderedMove> ordered;
  ordered.reserve(moves.size());
  for (const Move& move : moves) {
    OrderedMove item;
    item.move = move;
    item.capture = is_capture(move);
    item.see = move_see(board, move, result);
    if (qsearch && result != nullptr && (item.capture || move.is_promotion())) {
      ++result->q_see_calls;
    }
    // Check detection requires make/unmake and dominates NPS if performed for
    // every legal move at every main-search node.  Main search probes it only
    // for an otherwise reducible late quiet move; qsearch needs it for its
    // tactical pruning exceptions.
    bool calculate_check = include_checks;
#if HEBICHESS_QSEARCH_LAZY_CHECKS
    if (qsearch && include_checks) {
      if (qsearch_evasion) {
        calculate_check = false;
      } else if (move.is_promotion()) {
        calculate_check = false;
      } else if (item.capture && item.see >= 0) {
        const bool delta_candidate = qsearch_stand_pat.has_value() && qsearch_alpha.has_value() &&
            *qsearch_stand_pat + piece_value(
                move.flag == MoveFlag::EnPassant ? PieceType::Pawn : board.piece_at(move.to).type) +
                QSEARCH_DELTA_MARGIN_CP < *qsearch_alpha;
        calculate_check = delta_candidate;
      }
    }
#endif
    item.gives_check = calculate_check && gives_check(board, move);
    if (qsearch && include_checks && result != nullptr) {
      if (calculate_check) {
        ++result->q_gives_check_calls;
        if (qsearch_evasion) ++result->q_evasion_check_ordering_calls;
        else if (item.capture && item.see < -100) ++result->q_gives_check_for_see_exception;
        else ++result->q_gives_check_for_ordering;
      } else {
        ++result->q_gives_check_skipped;
      }
    }
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

struct StyleCancellation {
  bool has_deadline{false};
  std::chrono::steady_clock::time_point deadline{};
  bool timed_out{false};

  bool should_cancel() {
    if (!timed_out && has_deadline && std::chrono::steady_clock::now() >= deadline)
      timed_out = true;
    return timed_out;
  }
};

bool count_sacrifice_motifs(const Board& board, Color attacker,
                            StyleCancellation& cancellation, int& motifs) {
  StyleTimer motif_timer(&SearchResult::style_sacrifice_motifs_time_us);
  if (cancellation.should_cancel()) return false;
  Board motif_board = board;
  motif_board.set_side_to_move(attacker);
  const StyleAttackState before = style_attack_state(motif_board, attacker);
  motifs = 0;
  if (active_style_profile != nullptr) ++active_style_profile->result.style_legal_move_generations;
  for (const Move& move : generate_legal_moves(motif_board)) {
    if (cancellation.should_cancel()) return false;
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
  return !cancellation.should_cancel();
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
                                  bool calculate_sacrifice_motifs,
                                  StyleCancellation& cancellation) {
  if (cancellation.should_cancel()) return;
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
    int motifs = 0;
    if (context.before_sacrifice_motifs < 0) {
      if (!count_sacrifice_motifs(before, attacker, cancellation, motifs)) return;
      context.before_sacrifice_motifs = motifs;
    }
    if (!count_sacrifice_motifs(after, attacker, cancellation, motifs)) return;
    after_attack.sacrifice_motifs = motifs;
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

#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
void set_qsearch_tt_cutoff_trace_callback_for_test(
    QsearchTtCutoffTraceCallback callback) {
  qsearch_tt_cutoff_trace_callback = std::move(callback);
}
#endif

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
                    SearchContext& context,
                    const NnueAccumulator* accumulator = nullptr) {
  if (context.should_stop()) return 0;
  auto& nodes = context.nodes;
  auto& qnodes = context.qnodes;
  ++nodes;
  ++qnodes;
  const Color side = board.side_to_move();
  const Square king = board.find_king(side);
  const bool in_check = king.is_valid() &&
                        board.is_square_attacked(king, opposite(side));
  if (context.result != nullptr) {
    if (in_check) ++context.result->q_in_check_nodes;
    else ++context.result->q_non_check_nodes;
    context.result->q_max_ply = std::max(context.result->q_max_ply,
                                        static_cast<std::uint64_t>(std::max(0, ply)));
  }
  const int original_alpha = alpha;
  const int original_beta = beta;

#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
  std::optional<std::size_t> trace_index;
  if (context.qsearch_window_trace != nullptr) {
    QsearchNodeTrace node;
    node.sequence = context.qsearch_window_trace->size();
    node.key = board.zobrist_key();
    node.fen = board.to_fen();
    node.ply = ply;
    node.entry_alpha = alpha;
    node.entry_beta = beta;
    node.in_check = in_check;
    node.raw_nnue = raw_nnue(board, accumulator);
    node.accumulator_checksum = accumulator_checksum(accumulator);
    node.alpha_after_stand_pat = alpha;
    context.qsearch_window_trace->push_back(std::move(node));
    trace_index = context.qsearch_window_trace->size() - 1;
  }
  const auto finish_trace = [&](QsearchReturnKind kind, int value) {
    if (trace_index) {
      QsearchNodeTrace& node = context.qsearch_window_trace->at(*trace_index);
      node.return_kind = kind;
      node.returned_score = value;
    }
  };
#endif

#if HEBICHESS_QSEARCH_TT_VARIANT != 0
  // C1 eligibility is deliberately narrow: no probe, store, or cutoff at an
  // in-check qsearch node.  QTT never supplies a move-ordering hint.
  TranspositionTable* qtt = nullptr;
  ZobristKey qtt_key = 0;
  if (!in_check
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
      && !context.qtt_diagnostic_disable
#endif
      ) {
    qtt = &qsearch_transposition_table();
    qtt_key = board.zobrist_key();
    const TTEntry* qtt_entry = qtt->probe(qtt_key);
    int qtt_score = 0;
    bool qtt_window_reusable = false;
    bool qtt_reusable_subtree = false;
    if (context.result != nullptr) {
      ++context.result->qtt_probes;
      ++context.result->qtt_non_check_probes;
#if HEBICHESS_QSEARCH_TT_PROFILE
#if HEBICHESS_QSEARCH_TT_VARIANT == 2
      ++context.result->qtt_active_probes;
#endif
#endif
    }
    if (qtt_entry != nullptr) {
      qtt_score = score_from_tt(qtt_entry->score, ply);
      const int cutoff_bit = qtt_entry->bound == TTBound::Exact ? QTT_EXACT_CUTOFF
                           : qtt_entry->bound == TTBound::Lower ? QTT_LOWER_CUTOFF
                           : QTT_UPPER_CUTOFF;
      qtt_window_reusable = qtt_entry->bound == TTBound::Exact ||
          (qtt_entry->bound == TTBound::Lower && qtt_score >= beta) ||
          (qtt_entry->bound == TTBound::Upper && qtt_score <= alpha);
      qtt_reusable_subtree = qtt_window_reusable &&
          (HEBICHESS_QSEARCH_TT_CUTOFF_MASK & cutoff_bit) != 0;
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
      if (context.qtt_diagnostic != nullptr && context.qtt_diagnostic->override_bounds) {
        qtt_reusable_subtree = qtt_window_reusable &&
            (context.qtt_diagnostic->bound_mask & cutoff_bit) != 0;
      }
#endif
      if (context.result != nullptr) {
        ++context.result->qtt_hits;
        ++context.result->qtt_same_position_repeats;
        ++context.result->qtt_non_check_hits;
#if HEBICHESS_QSEARCH_TT_PROFILE
        if (qtt_entry->bound == TTBound::Exact) ++context.result->qtt_exact_hits;
        if (qtt_entry->bound == TTBound::Lower) ++context.result->qtt_lower_hits;
        if (qtt_entry->bound == TTBound::Upper) ++context.result->qtt_upper_hits;
        if (qtt_entry->bound == TTBound::Exact)
          ++context.result->qtt_potential_reusable_eval;
        if (qtt_window_reusable) ++context.result->qtt_potential_reusable_subtree;
#if HEBICHESS_QSEARCH_TT_VARIANT == 2
        ++context.result->qtt_active_hits;
#endif
#endif
      }
      // C1 is intentionally cutoff-only: it does not feed a move back into
      // ordering, and this branch is already known to be a non-check node.
      if (qtt_reusable_subtree) {
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
        bool apply_cutoff = true;
        std::uint64_t cutoff_serial = 0;
        if (context.qtt_diagnostic != nullptr) {
          cutoff_serial = ++context.qtt_diagnostic->cutoff_serial;
          if (context.result != nullptr) ++context.result->qtt_cutoff_candidates;
          apply_cutoff = context.qtt_diagnostic->cutoff_limit < 0 ||
              cutoff_serial <= static_cast<std::uint64_t>(context.qtt_diagnostic->cutoff_limit);
        }
        if (apply_cutoff && context.qtt_diagnostic != nullptr &&
            qsearch_tt_cutoff_trace_callback &&
            (context.qtt_diagnostic->trace_cutoff == 0 ||
             context.qtt_diagnostic->trace_cutoff == cutoff_serial)) {
          const auto stored = context.qtt_diagnostic->stores.find(qtt_key);
          const QsearchTtStoreProvenance* provenance =
              stored == context.qtt_diagnostic->stores.end() ? nullptr : &stored->second;
          const auto qtt_off = [&](const Board& source, int oracle_alpha, int oracle_beta,
                                   int oracle_ply) {
            Board oracle_board = source;
            std::uint64_t oracle_nodes = 0, oracle_qnodes = 0;
            SearchContext oracle{oracle_nodes, oracle_qnodes, false, {}, false, nullptr,
                                 nullptr, context.use_see_pruning, context.heuristics,
                                 context.use_null_move, context.use_lmr, context.use_pvs,
                                 context.eval_mode, context.deadline_check_interval_nodes};
            oracle.qtt_diagnostic_disable = true;
            std::optional<NnueAccumulator> oracle_accumulator;
            if (context.eval_mode == EvalMode::NNUE && nnue_network_available()) {
              oracle_accumulator.emplace();
              if (!refresh_nnue_accumulator(oracle_board, *oracle_accumulator))
                oracle_accumulator.reset();
            }
            return quiescence_impl(oracle_board, oracle_alpha, oracle_beta, oracle_ply,
                                   oracle, oracle_accumulator ? &*oracle_accumulator : nullptr);
          };
          QsearchTtCutoffTrace event;
          event.cutoff_serial = cutoff_serial;
          event.key = qtt_key;
          event.hit_fen = board.to_fen();
          event.ply = ply;
          event.alpha = alpha;
          event.beta = beta;
          event.bound = qtt_entry->bound;
          event.stored_score = qtt_entry->score;
          event.decoded_score = qtt_score;
          event.hit_raw_nnue = raw_nnue(board, accumulator);
          event.hit_accumulator_checksum = accumulator_checksum(accumulator);
          event.qtt_off_hit_window = qtt_off(board, alpha, beta, ply);
          if (provenance != nullptr) {
            event.stored_key = provenance->key;
            event.store_serial = provenance->serial;
            event.store_fen = provenance->fen;
            event.store_ply = provenance->ply;
            event.store_alpha = provenance->alpha;
            event.store_beta = provenance->beta;
            event.store_result = provenance->result;
            event.same_fen = provenance->fen == event.hit_fen;
            event.same_full_key = provenance->key == qtt_key;
            event.store_raw_nnue = provenance->raw_nnue;
            event.store_accumulator_checksum = provenance->accumulator_checksum;
            if (const auto store_board = Board::from_fen(provenance->fen)) {
              event.qtt_off_store_window = qtt_off(*store_board, provenance->alpha,
                                                   provenance->beta, provenance->ply);
              event.qtt_off_full_window = qtt_off(*store_board, -MATE_SCORE,
                                                  MATE_SCORE, provenance->ply);
            }
          }
          qsearch_tt_cutoff_trace_callback(event);
        }
        if (!apply_cutoff) {
          // The serial limit shadows this otherwise valid C1 cutoff while
          // leaving probe/store behavior unchanged for binary isolation.
          qtt_reusable_subtree = false;
        } else if (context.result != nullptr) {
          ++context.result->qtt_cutoff_applied;
        }
#endif
        if (!qtt_reusable_subtree) {
          // Continue into ordinary QSearch after a diagnostic-only shadow.
        } else {
#if HEBICHESS_QSEARCH_TT_PROFILE
        if (context.result != nullptr) ++context.result->qtt_active_cutoffs;
#endif
        if (context.result != nullptr) ++context.result->qtt_cutoffs;
        return qtt_score;
        }
      }
    }
  }
  const auto store_qtt = [&](int score) {
    if (qtt == nullptr) return;
    const TTBound bound = score <= original_alpha ? TTBound::Upper
                        : score >= original_beta ? TTBound::Lower : TTBound::Exact;
    const TTStoreResult stored = qtt->store(qtt_key, 0, score_to_tt(score, ply), bound,
                                            std::nullopt);
#if HEBICHESS_QSEARCH_TT_PROFILE
    if (context.result != nullptr && stored.stored) {
      ++context.result->qtt_stores;
      if (stored.replaced_different_key) ++context.result->qtt_replacements;
    }
#endif
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
    if (context.qtt_diagnostic != nullptr) {
      QsearchTtStoreProvenance provenance;
      provenance.key = qtt_key;
      provenance.serial = ++context.qtt_diagnostic->store_serial;
      provenance.fen = board.to_fen();
      provenance.ply = ply;
      provenance.alpha = original_alpha;
      provenance.beta = original_beta;
      provenance.result = score;
      provenance.bound = bound;
      provenance.encoded_score = score_to_tt(score, ply);
      provenance.raw_nnue = raw_nnue(board, accumulator);
      provenance.accumulator_checksum = accumulator_checksum(accumulator);
      context.qtt_diagnostic->stores[qtt_key] = std::move(provenance);
    }
#endif
  };
#else
  const auto store_qtt = [](int) {};
#endif
  const std::vector<Move> legal = in_check ? generate_legal_moves(board) : std::vector<Move>{};
  if (in_check && context.result != nullptr) {
    ++context.result->q_full_legal_movegen_calls;
    context.result->q_check_evasion_generated += legal.size();
  }

  if (in_check) {
    if (legal.empty()) {
      const int score = -MATE_SCORE + ply;
      store_qtt(score);
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
      finish_trace(QsearchReturnKind::Mate, score);
#endif
      return score;
    }
    const auto evasions = order_moves(board, legal, context.result, context.heuristics, ply,
                                      std::nullopt, true, true, true);
    int best = -MATE_SCORE;
    for (std::size_t order = 0; order < evasions.size(); ++order) {
      const OrderedMove& item = evasions[order];
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
      std::optional<std::size_t> trace_move_index;
      if (trace_index) {
        QsearchMoveTrace move_trace;
        move_trace.move = move_to_uci(item.move);
        move_trace.capture = item.capture;
        move_trace.promotion = item.move.is_promotion();
        move_trace.gives_check = item.gives_check;
        move_trace.see = item.see;
        move_trace.order = static_cast<int>(order);
        move_trace.searched = true;
        move_trace.child_alpha = -beta;
        move_trace.child_beta = -alpha;
        QsearchNodeTrace& node = context.qsearch_window_trace->at(*trace_index);
        node.moves.push_back(std::move(move_trace));
        trace_move_index = node.moves.size() - 1;
      }
#endif
      std::optional<NnueAccumulator> child_accumulator;
      if (context.result != nullptr) ++context.result->q_check_evasion_searched;
      const NnueAccumulator* child = nullptr;
      if (accumulator != nullptr) {
        child_accumulator.emplace();
        child = make_child_accumulator(board, item.move, accumulator,
                                       *child_accumulator, context, true);
      }
      const UndoState undo = board.make_move(item.move);
      const int score = -quiescence_impl(board, -beta, -alpha, ply + 1,
                                         context, child);
      board.unmake_move(item.move, undo);
      if (context.stopped) {
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
        if (trace_move_index)
          context.qsearch_window_trace->at(*trace_index).moves.at(*trace_move_index).child_score = score;
        finish_trace(QsearchReturnKind::Stopped, 0);
#endif
        return 0;
      }
      if (score > best) {
        best = score;
      }
      alpha = std::max(alpha, score);
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
      if (trace_move_index) {
        QsearchMoveTrace& move_trace =
            context.qsearch_window_trace->at(*trace_index).moves.at(*trace_move_index);
        move_trace.child_score = score;
        move_trace.alpha_after = alpha;
        move_trace.beta_cutoff = alpha >= beta;
      }
#endif
      if (alpha >= beta) break;
    }
    store_qtt(best);
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
    finish_trace(QsearchReturnKind::EvasionLoop, best);
#endif
    return best;
  }

  const int stand_pat = evaluate_search_position(board, context, accumulator, true);
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
  if (trace_index) context.qsearch_window_trace->at(*trace_index).stand_pat = stand_pat;
#endif
  if (stand_pat >= beta) {
    if (context.result != nullptr) ++context.result->q_stand_pat_beta_cutoffs;
    store_qtt(beta);
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
    if (trace_index) context.qsearch_window_trace->at(*trace_index).alpha_after_stand_pat = alpha;
    finish_trace(QsearchReturnKind::StandPatBeta, beta);
#endif
    return beta;
  }
  alpha = std::max(alpha, stand_pat);
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
  if (trace_index) context.qsearch_window_trace->at(*trace_index).alpha_after_stand_pat = alpha;
#endif

  const std::vector<Move> tactical_moves = generate_legal_tactical_moves(board);
  if (context.result != nullptr) {
    ++context.result->q_tactical_movegen_calls;
    context.result->q_tactical_generated += tactical_moves.size();
  }
  if (tactical_moves.empty() && generate_legal_moves(board).empty()) {
    if (context.result != nullptr) {
      ++context.result->q_stalemate_full_movegen_calls;
      ++context.result->q_full_legal_movegen_calls;
    }
    store_qtt(0);
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
    finish_trace(QsearchReturnKind::Stalemate, 0);
#endif
    return 0;
  }
  const auto tactical = order_moves(board, tactical_moves, context.result, nullptr, ply,
                                    std::nullopt, true, true, false, stand_pat, alpha);
  for (std::size_t order = 0; order < tactical.size(); ++order) {
    const OrderedMove& item = tactical[order];
    const Move& move = item.move;
    const bool promotion = move.is_promotion();
    const bool capture = item.capture;
    const bool check = item.gives_check;
    const int see = item.see;
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
    std::optional<std::size_t> trace_move_index;
    if (trace_index) {
      QsearchMoveTrace move_trace;
      move_trace.move = move_to_uci(move);
      move_trace.capture = capture;
      move_trace.promotion = promotion;
      move_trace.gives_check = check;
      move_trace.see = see;
      move_trace.order = static_cast<int>(order);
      QsearchNodeTrace& node = context.qsearch_window_trace->at(*trace_index);
      node.moves.push_back(std::move(move_trace));
      trace_move_index = node.moves.size() - 1;
    }
#endif
    if (context.use_see_pruning && capture && see < -100 && !check && !promotion) {
      if (context.result != nullptr) {
        ++context.result->see_prunes;
        ++context.result->q_see_pruned;
      }
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
      if (trace_move_index)
        context.qsearch_window_trace->at(*trace_index).moves.at(*trace_move_index).see_rejected = true;
#endif
      continue;
    }
#if HEBICHESS_QSEARCH_DELTA_PRUNING
    // Legacy production rule.  No-delta experiment targets compile this
    // entire rejection out; the default remains enabled so production and
    // all pre-existing targets retain byte-for-byte search semantics.
    const Piece victim = move.flag == MoveFlag::EnPassant
        ? Piece{PieceType::Pawn, opposite(side)} : board.piece_at(move.to);
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
    const bool disable_delta_pruning = context.qsearch_diagnostic_disable_delta_pruning;
#else
    constexpr bool disable_delta_pruning = false;
#endif
    if (!disable_delta_pruning && capture && !check && !promotion &&
        stand_pat + piece_value(victim.type) + QSEARCH_DELTA_MARGIN_CP < alpha) {
      if (context.result != nullptr) {
        ++context.result->qdelta_prunes;
        ++context.result->q_delta_pruned;
      }
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
      if (trace_move_index)
        context.qsearch_window_trace->at(*trace_index).moves.at(*trace_move_index).delta_rejected = true;
#endif
      continue;
    }
#endif
#if HEBICHESS_QSEARCH_LAZY_CHECKS
    if (capture && !promotion && check &&
        stand_pat + piece_value(victim.type) + QSEARCH_DELTA_MARGIN_CP < alpha &&
        context.result != nullptr) {
      ++context.result->q_gives_check_for_delta_exception;
    }
#endif
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
    if (trace_move_index) {
      QsearchMoveTrace& move_trace =
          context.qsearch_window_trace->at(*trace_index).moves.at(*trace_move_index);
      move_trace.searched = true;
      move_trace.child_alpha = -beta;
      move_trace.child_beta = -alpha;
    }
#endif
    std::optional<NnueAccumulator> child_accumulator;
    if (context.result != nullptr) ++context.result->q_tactical_searched;
    const NnueAccumulator* child = nullptr;
    if (accumulator != nullptr) {
      child_accumulator.emplace();
      child = make_child_accumulator(board, move, accumulator,
                                     *child_accumulator, context, true);
    }
    const UndoState undo = board.make_move(move);
    const int score = -quiescence_impl(board, -beta, -alpha, ply + 1,
                                       context, child);
    board.unmake_move(move, undo);
    if (context.stopped) {
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
      if (trace_move_index)
        context.qsearch_window_trace->at(*trace_index).moves.at(*trace_move_index).child_score = score;
      finish_trace(QsearchReturnKind::Stopped, 0);
#endif
      return 0;
    }
    alpha = std::max(alpha, score);
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
    if (trace_move_index) {
      QsearchMoveTrace& move_trace =
          context.qsearch_window_trace->at(*trace_index).moves.at(*trace_move_index);
      move_trace.child_score = score;
      move_trace.alpha_after = alpha;
      move_trace.beta_cutoff = alpha >= beta;
    }
#endif
    if (alpha >= beta) break;
  }
  store_qtt(alpha);
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
  finish_trace(QsearchReturnKind::TacticalLoop, alpha);
#endif
  return alpha;
}

int negamax_impl(Board& board, int depth, int alpha, int beta, int ply,
                 SearchContext& context, const NnueAccumulator* accumulator = nullptr,
                 bool was_null_move = false) {
  if (context.should_stop()) return 0;
  if (depth <= 0) return quiescence_impl(board, alpha, beta, ply, context, accumulator);
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
#if defined(HEBICHESS_NNUE_SEARCH_TEST)
    if (accumulator != nullptr && context.nnue_counters != nullptr)
      ++context.nnue_counters->null_move_accumulator_reuse_count;
#endif
    const int score = -negamax_impl(board, depth - 1 - reduction,
                                    -beta, -beta + 1, ply + 1, context, accumulator, true);
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
    std::optional<NnueAccumulator> child_accumulator;
    const NnueAccumulator* child = nullptr;
    if (accumulator != nullptr) {
      child_accumulator.emplace();
      child = make_child_accumulator(board, move, accumulator,
                                     *child_accumulator, context, false);
    }
    const UndoState undo = board.make_move(move);
    int score = 0;
    const auto search_child = [&](int child_depth, bool zero_window) {
      if (zero_window && context.result != nullptr)
        ++context.result->pvs_zero_window_searches;
      if (zero_window)
        return -negamax_impl(board, child_depth, -alpha - 1, -alpha,
                             ply + 1, context, child);
      return -negamax_impl(board, child_depth, -beta, -alpha, ply + 1,
                           context, child);
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
    const TTStoreResult stored = context.tt->store(board.zobrist_key(), depth,
                                                    score_to_tt(best, ply), bound,
                                                    best_move);
    #if HEBICHESS_QSEARCH_TT_PROFILE
    if (stored.stored) {
      ++context.result->tt_stores;
      if (stored.replaced_different_key) ++context.result->tt_replacements;
    }
    #endif
  }
  return best;
}

int negamax(Board& board, int depth, int alpha, int beta, int ply,
            std::uint64_t& nodes) {
#if HEBICHESS_QSEARCH_TT_VARIANT != 0
  clear_qsearch_transposition_table();
#endif
  std::uint64_t qnodes = 0;
  SearchContext context{nodes, qnodes};
  return negamax_impl(board, depth, alpha, beta, ply, context);
}

int quiescence(Board& board, int alpha, int beta, int ply) {
#if HEBICHESS_QSEARCH_TT_VARIANT != 0
  clear_qsearch_transposition_table();
#endif
  std::uint64_t nodes = 0;
  std::uint64_t qnodes = 0;
  SearchContext context{nodes, qnodes};
  return quiescence_impl(board, alpha, beta, ply, context);
}

#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
QsearchWindowTrace trace_qsearch_window_for_test(const Board& board, int alpha, int beta,
                                                  int ply, EvalMode eval_mode,
                                                  bool disable_delta_pruning) {
  QsearchWindowTrace trace;
  Board working = board;
  std::uint64_t nodes = 0;
  std::uint64_t qnodes = 0;
  SearchContext context{nodes, qnodes, false, {}, false, nullptr, nullptr, true,
                        nullptr, false, false, false, eval_mode, 1};
  context.qtt_diagnostic_disable = true;
  context.qsearch_window_trace = &trace.nodes;
  context.qsearch_diagnostic_disable_delta_pruning = disable_delta_pruning;
  std::optional<NnueAccumulator> accumulator;
  if (eval_mode == EvalMode::NNUE && nnue_network_available()) {
    accumulator.emplace();
    if (!refresh_nnue_accumulator(working, *accumulator)) accumulator.reset();
  }
  trace.score = quiescence_impl(working, alpha, beta, ply, context,
                                accumulator ? &*accumulator : nullptr);
  return trace;
}
#endif

SearchResult search(const Board& position, int max_depth) {
  SearchLimits limits;
  limits.max_depth = max_depth;
  return search(position, limits);
}

SearchResult search_impl(const Board& position, const SearchLimits& limits,
                         const SearchInfoCallback& on_iteration
#if defined(HEBICHESS_NNUE_SEARCH_TEST)
                         , bool test_use_nnue_accumulator,
                         NnueSearchAccumulatorCounters* nnue_counters
#endif
                         ) {
  SearchResult result;
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
  QsearchTtDiagnosticState qtt_diagnostic;
  qtt_diagnostic.override_bounds = limits.qtt_diagnostic_override_bounds;
  qtt_diagnostic.bound_mask = limits.qtt_diagnostic_bound_mask;
  qtt_diagnostic.cutoff_limit = limits.qtt_cutoff_limit;
  qtt_diagnostic.trace_cutoff = limits.qtt_trace_cutoff;
#endif
  const auto search_started = std::chrono::steady_clock::now();
  auto update_final_elapsed = [&]() {
    result.time_elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - search_started).count());
  };
  if (limits.max_depth < 1) {
    update_final_elapsed();
    return result;
  }
  if (limits.has_soft_deadline)
    result.time_soft_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(limits.soft_deadline - search_started).count());
  if (limits.has_deadline)
    result.time_hard_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(limits.deadline - search_started).count());
  result.early_budget = limits.has_deadline && limits.has_soft_deadline &&
      result.time_hard_ms <= 15000 && result.time_hard_ms > result.time_soft_ms + 3500;
  if (limits.has_soft_deadline)
    result.time_target_ms = adaptive_target_ms(result.time_soft_ms, result.time_hard_ms, TimeConfidence::Low);
  result.time_confidence = "low";
  Board root = position;
  const bool use_nnue_accumulator = limits.eval_mode == EvalMode::NNUE &&
      nnue_network_available()
#if defined(HEBICHESS_NNUE_SEARCH_TEST)
      && test_use_nnue_accumulator
#endif
      ;
  std::optional<NnueAccumulator> root_accumulator;
  if (use_nnue_accumulator) {
    root_accumulator.emplace();
    if (!refresh_nnue_accumulator(root, *root_accumulator)) root_accumulator.reset();
#if defined(HEBICHESS_NNUE_SEARCH_TEST)
    else if (nnue_counters != nullptr) ++nnue_counters->root_full_refresh_count;
#endif
  }
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
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
    context.qtt_diagnostic = &qtt_diagnostic;
#endif
#if defined(HEBICHESS_NNUE_SEARCH_TEST)
    context.use_nnue_accumulator = test_use_nnue_accumulator;
    context.nnue_counters = nnue_counters;
#endif
    result.score = negamax_impl(root, 0, -MATE_SCORE, MATE_SCORE, 0, context,
                                root_accumulator ? &*root_accumulator : nullptr);
    update_final_elapsed();
    return result;
  }
  result.best_move = legal.front();
  if (legal.size() == 1) {
    result.time_stop_reason = "forced";
    update_final_elapsed();
    return result;
  }
  const Square enemy_king = root.find_king(opposite(root.side_to_move()));
  // Preserve the explicit benchmark setting as result metadata only.  It no
  // longer reserves objective-search time or influences its deadline.
  if (limits.has_deadline && limits.style_verification_reserve_ms >= 0) {
    result.style_verification_reserve_active = true;
    result.style_verification_reserve_ms =
        static_cast<std::uint64_t>(limits.style_verification_reserve_ms);
  }
  const auto objective_deadline = limits.deadline;
  std::vector<RootMoveInfo> previous_root;
  std::vector<RootMoveInfo> last_completed;
  int last_objective_best = -MATE_SCORE;
  std::vector<int> best_move_history;
  std::vector<int> score_history;
  std::vector<bool> aspiration_retry_history;
  int target_floor_ms = 0;
  int reuse_best_streak = 0;
  auto move_key = [](const Move& move) {
    return static_cast<int>(move.from.index()) * 64 + move.to.index();
  };
  auto confidence_name = [](TimeConfidence confidence) {
    switch (confidence) {
      case TimeConfidence::High: return "high";
      case TimeConfidence::Medium: return "medium";
      case TimeConfidence::Low: return "low";
      case TimeConfidence::VeryLow: return "very_low";
    }
    return "low";
  };
  for (int depth = 1; depth <= limits.max_depth; ++depth) {
    result.attempted_depth = depth;
    const auto iteration_started = std::chrono::steady_clock::now();
    const std::uint64_t aspiration_retries_before = result.aspiration_retries;
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
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
      context.qtt_diagnostic = &qtt_diagnostic;
#endif
#if defined(HEBICHESS_NNUE_SEARCH_TEST)
      context.use_nnue_accumulator = test_use_nnue_accumulator;
      context.nnue_counters = nnue_counters;
#endif
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
      if (limits.reuse_hit && limits.has_prepared_root_move) {
        for (OrderedMove& item : root_order)
          if (item.move == limits.prepared_root_move) item.score += 20000000;
        std::sort(root_order.begin(), root_order.end(), [](const OrderedMove& a, const OrderedMove& b) {
          return a.score != b.score ? a.score > b.score : move_order_less(a.move, b.move);
        });
      }
      for (std::size_t move_index = 0; move_index < root_order.size(); ++move_index) {
        const Move& move = root_order[move_index].move;
        if (context.should_stop()) break;
        std::optional<NnueAccumulator> child_accumulator;
        const NnueAccumulator* child = nullptr;
        if (root_accumulator.has_value()) {
          child_accumulator.emplace();
          child = make_child_accumulator(root, move, &*root_accumulator,
                                         *child_accumulator, context, false);
        }
        const UndoState undo = root.make_move(move);
        const int search_alpha = alpha;
        const int search_beta = beta;
        int score = 0;
        const bool zero_window = limits.use_pvs && move_index > 0;
        bool pvs_full_research = false;
        ScoreBound bound = ScoreBound::Exact;
        if (zero_window) {
          ++result.pvs_zero_window_searches;
          score = -negamax_impl(root, depth - 1, -alpha - 1, -alpha, 1, context, child);
          if (!context.stopped && score > alpha && score < beta) {
            ++result.pvs_researches;
            score = -negamax_impl(root, depth - 1, -beta, -alpha, 1, context, child);
            pvs_full_research = true;
            // A completed full re-search supersedes the zero-window bound.
            // Classify it against the window that was actually searched.
            if (score <= search_alpha) bound = ScoreBound::Upper;
            else if (score >= search_beta) bound = ScoreBound::Lower;
          } else if (score <= alpha) {
            bound = ScoreBound::Upper;
          } else {
            bound = ScoreBound::Lower;
          }
        } else {
          score = -negamax_impl(root, depth - 1, -beta, -alpha, 1, context, child);
          // The first root move is normally searched with a full window.  A
          // fail-low must not become Exact simply because alpha is unchanged.
          if (score <= search_alpha) bound = ScoreBound::Upper;
          else if (score >= search_beta) bound = ScoreBound::Lower;
        }
        root.unmake_move(move, undo);
        if (context.stopped) break;
        current.push_back({move, score, bound});
        RootMoveInfo& root_info = current.back();
        root_info.search_alpha = search_alpha;
        root_info.search_beta = search_beta;
        root_info.pvs_full_research = pvs_full_research;
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
    result.latest_iteration_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - iteration_started).count());
    if (stopped_iteration || current.size() != legal.size()) {
      if (limits.has_deadline && std::chrono::steady_clock::now() >= limits.deadline)
        result.time_stop_reason = "hard";
      break;
    }
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

    best_move_history.push_back(move_key(objective_best_move));
    score_history.push_back(objective_best);
    aspiration_retry_history.push_back(result.aspiration_retries != aspiration_retries_before);
    int second_best = -MATE_SCORE;
    bool root_margin_known = objective_move->bound == ScoreBound::Exact;
    bool usable_competitor = false;
    for (const RootMoveInfo& info : last_completed) {
      if (info.move == objective_best_move) continue;
      if (info.bound == ScoreBound::Lower) {
        // A lower bound has no useful ceiling: the true competitor score may
        // be arbitrarily above this reported value.
        root_margin_known = false;
      } else {
        usable_competitor = true;
        second_best = std::max(second_best, info.search_score);
      }
    }
    root_margin_known = root_margin_known && usable_competitor;
    const int root_margin = root_margin_known ? objective_best - second_best : 0;
    const TimeEvidence final_evidence{best_move_history, score_history,
        aspiration_retry_history, root_margin_known, root_margin};
    const TimeConfidence confidence = choose_time_confidence(final_evidence);
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = now - search_started;
    const int elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    const int soft_ms = limits.has_soft_deadline ? static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(limits.soft_deadline - search_started).count()) : 0;
    const int hard_ms = limits.has_deadline ? static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(limits.deadline - search_started).count()) : 0;
    const bool confidence_evidence_ready = best_move_history.size() >= 3 &&
        score_history.size() >= 3;
    if (limits.has_soft_deadline) {
      target_floor_ms = update_adaptive_target_floor(
          target_floor_ms, soft_ms, hard_ms, confidence,
          confidence_evidence_ready);
    }
    const int target_ms = limits.has_soft_deadline
        ? (confidence_evidence_ready
            ? target_floor_ms
            : adaptive_target_ms(soft_ms, hard_ms, TimeConfidence::Low))
        : 0;
    result.time_soft_ms = soft_ms;
    result.time_hard_ms = hard_ms;
    result.time_target_ms = target_ms;
    result.time_elapsed_ms = elapsed_ms;
    result.time_stability = 1;
    for (std::size_t i = best_move_history.size(); i > 1 &&
         best_move_history[i - 1] == best_move_history.back(); --i) ++result.time_stability;
    result.time_score_swing = 0;
    if (score_history.size() >= 2) {
      const auto first = score_history.size() - std::min<std::size_t>(3, score_history.size());
      const auto range = std::minmax_element(score_history.begin() + first, score_history.end());
      result.time_score_swing = *range.second - *range.first;
    }
    result.time_margin_known = root_margin_known;
    result.time_margin = root_margin;
    result.time_confidence = confidence_name(confidence);
    const bool mate_confirmed = objective_best > MATE_SCORE - 1000 ||
        objective_best < -MATE_SCORE + 1000;
    const bool hard_reached = limits.has_deadline && now >= limits.deadline;
    const bool target_reached = limits.has_soft_deadline && target_ms > 0 && elapsed_ms >= target_ms;
    if (limits.reuse_hit && limits.has_prepared_root_move) {
      if (objective_best_move == limits.prepared_root_move &&
          result.aspiration_retries == aspiration_retries_before &&
          result.time_score_swing <= 60) ++reuse_best_streak;
      else reuse_best_streak = 0;
      const int verification_depth = std::max(4, limits.reuse_previous_depth - 2);
      if (reuse_best_streak >= 2 && depth >= verification_depth &&
          elapsed_ms >= std::max(350, limits.reuse_verification_ms)) {
        result.reuse_verified = true;
        result.reuse_fast_stop = true;
        result.time_stop_reason = "reuse_target";
        break;
      }
    }
    if (hard_reached || target_reached || (legal.size() == 1) || mate_confirmed) {
      result.time_stop_reason = hard_reached ? "hard" : legal.size() == 1 ? "forced" :
          mate_confirmed ? "mate" : "target";
      break;
    }
  }
  // Iteration telemetry is updated only after completed iterations.  Refresh
  // the wall-clock value once more after the search loop so a hard-stopped
  // partial iteration reports the actual total search duration.
  update_final_elapsed();
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
  if (!limits.use_root_style_selection) {
    // Diagnostic-only objective sweeps stop after the completed root search.
    // The default follows the existing production style-selection path.
    result.main_nodes = result.nodes - result.qnodes;
    return result;
  }
  auto& current = last_completed;
  const int depth = result.completed_depth;
  const int objective_best = last_objective_best;
  result.style_verification_eval_mode = limits.eval_mode;
  const auto metadata_started = std::chrono::steady_clock::now();
  StyleCancellation style_cancellation{limits.has_deadline, limits.deadline};
  StyleProfile profile{result};
  StyleProfile* const previous_profile = active_style_profile;
  auto objective_move = std::max_element(current.begin(), current.end(),
      [](const RootMoveInfo& a, const RootMoveInfo& b) {
        if (a.bound != ScoreBound::Exact) return true;
        if (b.bound != ScoreBound::Exact) return false;
        return a.search_score < b.search_score;
      });
  auto finish_objective_fallback = [&](const char* reason) {
    active_style_profile = previous_profile;
    result.time_stop_reason = reason;
    result.best_move = objective_move->move;
    result.score = objective_best;
    result.root_moves = std::move(current);
    result.main_nodes = result.nodes - result.qnodes;
    update_final_elapsed();
    return result;
  };
  // A move farther than the hard 50cp cap can never be selected by v3.2.
  // Analyse only final exact eligible root moves, sharing immutable before
  // state and a single child board per candidate.
  if (limits.profile_style_metadata) active_style_profile = &profile;
  if (style_cancellation.should_cancel()) {
    result.style_metadata_time_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - metadata_started).count());
    return finish_objective_fallback("hard_style_metadata");
  }
  RootStyleContext style_context = make_root_style_context(position);
  for (RootMoveInfo& info : current) {
    if (style_cancellation.should_cancel()) {
      result.style_metadata_time_ms = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - metadata_started).count());
      return finish_objective_fallback("hard_style_metadata");
    }
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
                                 limits.use_style_v3 && loss <= 50, style_cancellation);
    if (style_cancellation.should_cancel()) {
      result.style_metadata_time_ms = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - metadata_started).count());
      return finish_objective_fallback("hard_style_metadata");
    }
    ++result.style_evaluations;
  }
  active_style_profile = previous_profile;
  result.root_style_metadata_time_us += static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - metadata_started).count());
  result.style_metadata_time_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - metadata_started).count());
  if (style_cancellation.should_cancel())
    return finish_objective_fallback("hard_style_metadata");
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
  for (RootMoveInfo& info : current) {
    if (info.bound == ScoreBound::Exact) {
      info.style_safe = is_style_score_safe(objective_best, info.search_score,
                                             info.style_tolerance);
      info.style_proof = StyleProofResult::ExactScore;
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
    if (style_cancellation.should_cancel())
      return finish_objective_fallback("hard_style_metadata");
    if (&info == &*objective_move || info.bound == ScoreBound::Exact) continue;
    ++result.root_style_candidates;
    if (limits.use_style_v3 &&
        !(info.sacrifice_candidate || info.king_break || info.sacrifice_preparation ||
          info.style_tolerance > AGGRESSION_TOLERANCE_CP)) {
      info.style_proof = StyleProofResult::PrefilterSkipped;
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
      info.style_proof = StyleProofResult::PrefilterSkipped;
      ++result.root_style_prefilter_skips;
      continue;
    }
    if (info.bound == ScoreBound::Upper &&
        !is_style_score_safe(objective_best, info.search_score, info.style_tolerance)) {
      info.style_proof = StyleProofResult::UpperBoundRejected;
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
  if (style_cancellation.should_cancel())
    return finish_objective_fallback("hard_style_metadata");
  const auto verification_started = std::chrono::steady_clock::now();
  if (style_cancellation.should_cancel()) {
    result.style_verification_time_ms = 0;
    return finish_objective_fallback("hard_style_verification");
  }
  SearchContext verification{result.nodes, result.qnodes, limits.has_deadline,
                             limits.deadline, false, limits.use_tt ? &tt : nullptr, &result,
                             limits.use_see_pruning,
                             limits.use_killer_history ? &search_heuristics() : nullptr,
                             limits.use_null_move, limits.use_lmr, limits.use_pvs,
                             limits.eval_mode, 1};
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
  verification.qtt_diagnostic = &qtt_diagnostic;
#endif
#if defined(HEBICHESS_NNUE_SEARCH_TEST)
  verification.use_nnue_accumulator = test_use_nnue_accumulator;
  verification.nnue_counters = nnue_counters;
#endif
  result.style_verification_eval_mode = verification.eval_mode;
  for (RootMoveInfo* candidate : candidates) {
    if (style_cancellation.should_cancel()) {
      result.style_verification_time_ms = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - verification_started).count());
      return finish_objective_fallback("hard_style_verification");
    }
    if (candidate->style_score < chosen->style_score) {
      ++result.root_style_prefilter_skips;
      continue;
    }
    ++result.root_style_verification_searches;
    const std::uint64_t before_verification = result.nodes;
    const auto candidate_started = std::chrono::steady_clock::now();
    std::optional<NnueAccumulator> child_accumulator;
    const NnueAccumulator* child = nullptr;
    if (root_accumulator.has_value()) {
      child_accumulator.emplace();
      child = make_child_accumulator(root, candidate->move, &*root_accumulator,
                                     *child_accumulator, verification, false);
    }
    const UndoState undo = root.make_move(candidate->move);
    const int threshold = objective_best - candidate->style_tolerance;
    const int proof = -negamax_impl(root, depth - 1, -threshold, -threshold + 1, 1,
                                    verification, child);
    root.unmake_move(candidate->move, undo);
    const auto candidate_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - candidate_started).count());
    result.style_verification_max_ms = std::max(result.style_verification_max_ms, candidate_ms);
    result.root_style_verification_nodes += result.nodes - before_verification;
    if (verification.stopped) {
      ++result.root_style_verification_timeouts;
      result.style_verification_time_ms = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - verification_started).count());
      return finish_objective_fallback("hard_style_verification");
    }
    candidate->style_safe = proof >= threshold;
    if (candidate->style_safe) {
      candidate->style_proof = StyleProofResult::ThresholdProven;
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
    } else {
      candidate->style_proof = StyleProofResult::ThresholdRejected;
      ++result.root_style_rejected;
    }
  }
  result.style_verification_time_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - verification_started).count());
  if (style_cancellation.should_cancel())
    return finish_objective_fallback("hard_style_verification");
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

std::vector<Move> extract_principal_variation(const Board& board,
                                              const Move& root_move,
                                              int max_plies) {
  std::vector<Move> pv;
  if (max_plies <= 0) return pv;
  Board current = board;
  std::vector<ZobristKey> seen{current.zobrist_key()};
  std::optional<Move> next = root_move;
  for (int ply = 0; ply < max_plies; ++ply) {
    if (!next) break;
    const std::vector<Move> legal = generate_legal_moves(current);
    if (std::find(legal.begin(), legal.end(), *next) == legal.end()) break;
    const Move move = *next;
    pv.push_back(move);
    current.make_move(move);
    if (std::find(seen.begin(), seen.end(), current.zobrist_key()) != seen.end()) break;
    seen.push_back(current.zobrist_key());
    const TTEntry* entry = transposition_table().probe(current.zobrist_key());
    if (entry == nullptr || !entry->best_move) break;
    next = entry->best_move;
  }
  return pv;
}

SearchResult search(const Board& position, const SearchLimits& limits,
                    const SearchInfoCallback& on_iteration) {
#if defined(HEBICHESS_NNUE_SEARCH_TEST)
  SearchResult result = search_impl(position, limits, on_iteration, true, nullptr);
#else
  SearchResult result = search_impl(position, limits, on_iteration);
#endif
  result.principal_variation = extract_principal_variation(position, result.best_move);
  result.reuse_hit = limits.reuse_hit;
  result.reuse_prepared_depth = result.completed_depth;
  if (result.principal_variation.size() >= 2)
    result.reuse_expected = move_to_uci(result.principal_variation[1]);
  if (result.principal_variation.size() >= 3)
    result.reuse_prepared = move_to_uci(result.principal_variation[2]);
  return result;
}

#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
SearchResult search_forced_root_move_for_test(const Board& board,
                                              const Move& forced_root_move,
                                              const SearchLimits& limits) {
#if HEBICHESS_QSEARCH_TT_VARIANT != 0
  clear_qsearch_transposition_table();
#endif
  SearchResult result;
  QsearchTtDiagnosticState qtt_diagnostic;
  qtt_diagnostic.override_bounds = limits.qtt_diagnostic_override_bounds;
  qtt_diagnostic.bound_mask = limits.qtt_diagnostic_bound_mask;
  qtt_diagnostic.cutoff_limit = limits.qtt_cutoff_limit;
  qtt_diagnostic.trace_cutoff = limits.qtt_trace_cutoff;
  if (limits.max_depth < 1) return result;
  const std::vector<Move> legal = generate_legal_moves(board);
  if (std::find(legal.begin(), legal.end(), forced_root_move) == legal.end()) {
    return result;
  }

  Board root = board;
  const bool use_nnue_accumulator = limits.eval_mode == EvalMode::NNUE &&
      nnue_network_available();
  std::optional<NnueAccumulator> root_accumulator;
  if (use_nnue_accumulator) {
    root_accumulator.emplace();
    if (!refresh_nnue_accumulator(root, *root_accumulator)) root_accumulator.reset();
  }
  SearchContext context{result.nodes, result.qnodes, limits.has_deadline,
                        limits.deadline, false,
                        limits.use_tt ? &transposition_table() : nullptr, &result,
                        limits.use_see_pruning,
                        limits.use_killer_history ? &search_heuristics() : nullptr,
                        limits.use_null_move, limits.use_lmr, limits.use_pvs, limits.eval_mode,
                        limits.deadline_check_interval_nodes};
  context.qtt_diagnostic = &qtt_diagnostic;
  std::optional<NnueAccumulator> child_accumulator;
  const NnueAccumulator* child = nullptr;
  if (root_accumulator.has_value()) {
    child_accumulator.emplace();
    child = make_child_accumulator(root, forced_root_move, &*root_accumulator,
                                   *child_accumulator, context, false);
  }
  const UndoState undo = root.make_move(forced_root_move);
  result.score = -negamax_impl(root, limits.max_depth - 1, -MATE_SCORE, MATE_SCORE,
                               1, context, child);
  root.unmake_move(forced_root_move, undo);
  if (!context.stopped) {
    result.best_move = forced_root_move;
    result.completed_depth = limits.max_depth;
  }
  result.main_nodes = result.nodes - result.qnodes;
  return result;
}
#endif

#if defined(HEBICHESS_NNUE_SEARCH_TEST)
NnueSearchTestResult search_nnue_incremental_for_test(const Board& board,
                                                       const SearchLimits& limits) {
  NnueSearchTestResult result;
  result.search = search_impl(board, limits, {}, true, &result.counters);
  return result;
}

NnueSearchTestResult search_nnue_legacy_for_test(const Board& board,
                                                  const SearchLimits& limits) {
  NnueSearchTestResult result;
  result.search = search_impl(board, limits, {}, false, &result.counters);
  return result;
}
#endif

void clear_transposition_table() noexcept {
  transposition_table().clear();
#if HEBICHESS_QSEARCH_TT_VARIANT != 0
  clear_qsearch_transposition_table();
#endif
}

void clear_search_heuristics() noexcept { search_heuristics().clear(); }

}  // namespace hebichess
