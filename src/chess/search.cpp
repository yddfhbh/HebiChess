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

  bool should_stop() {
    if (stopped) return true;
    if ((nodes & 1023U) != 0) return false;
    if (has_deadline && std::chrono::steady_clock::now() >= deadline) {
      stopped = true;
    }
    return stopped;
  }
};

bool gives_check(Board& board, const Move& move) noexcept {
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
    return a.score > b.score;
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
  return static_cast<int>(generate_legal_moves(copy).size());
}

int material_loss(const Board& before, const Board& after, Color mover) {
  return evaluate_material(after, mover) - evaluate_material(before, mover);
}

}  // namespace

int evaluate_move_style(const Board& before, const Move& move,
                        const Board& after) noexcept {
  const Color mover = before.side_to_move();
  Board probe = before;
  const bool check = gives_check(probe, move);
  const int pressure_delta = evaluate_attack_pressure(after, mover) -
                             evaluate_attack_pressure(before, mover);
  const EvalBreakdown before_eval = evaluate_breakdown(before, mover);
  const EvalBreakdown after_eval = evaluate_breakdown(after, mover);
  const int escape_delta = count_king_escapes(after, opposite(mover)) -
                           count_king_escapes(before, opposite(mover));
  int style = (check ? 18 : 0) + std::max(0, pressure_delta) * 2 +
              std::max(0, -escape_delta) * 3 + (move.is_promotion() ? 12 : 0);
  style += std::max(0, after_eval.king_attack - before_eval.king_attack);
  style += std::max(0, after_eval.space - before_eval.space) / 2;
  style += std::max(0, after_eval.passed_pawns - before_eval.passed_pawns) / 2;
  style += std::max(0, after_eval.development - before_eval.development);
  const Piece moving = before.piece_at(move.from);
  const bool opening = game_phase(before) >= 18;
  const bool quiet_flank_pawn = opening && is_quiet_move(move) &&
                                moving.type == PieceType::Pawn &&
                                (move.from.file() == 0 || move.from.file() == 7);
  if (quiet_flank_pawn) style -= 6;
  if (opening && is_quiet_move(move) && moving.type == PieceType::Pawn &&
      (move.from.file() == 5 || move.from.file() == 6) &&
      before.find_king(mover).is_valid() && before.find_king(mover).file() >= 4 &&
      after_eval.king_safety < before_eval.king_safety) {
    style -= 4;
  }
  if (is_sacrifice_candidate(before, move, after)) style += check ? 12 : 5;
  return style;
}

bool is_sacrifice_candidate(const Board& before, const Move& move,
                            const Board& after) noexcept {
  const Color mover = before.side_to_move();
  const int loss = material_loss(before, after, mover);
  if (loss >= -30) return false;
  Board probe = before;
  const bool check = gives_check(probe, move);
  const int pressure_delta = evaluate_attack_pressure(after, mover) -
                             evaluate_attack_pressure(before, mover);
  const int escape_delta = count_king_escapes(after, opposite(mover)) -
                           count_king_escapes(before, opposite(mover));
  const EvalBreakdown before_eval = evaluate_breakdown(before, mover);
  const EvalBreakdown after_eval = evaluate_breakdown(after, mover);
  const bool attack_gain = after_eval.king_attack - before_eval.king_attack >= 8;
  const bool shield_break = after_eval.king_safety - before_eval.king_safety >= 8;
  return check || attack_gain || shield_break || pressure_delta >= 5 || escape_delta <= -2;
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

  const int stand_pat = evaluate(board);
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
                          limits.use_null_move, limits.use_lmr, limits.use_pvs};
    result.score = negamax_impl(root, 0, -MATE_SCORE, MATE_SCORE, 0, context);
    return result;
  }
  result.best_move = legal.front();
  // Root moves do not change between iterations.  Style is position-only, so
  // calculate it once; the more expensive sacrifice/SEE metadata remains lazy.
  std::vector<RootMoveInfo> root_style_cache;
  root_style_cache.reserve(legal.size());
  for (const Move& move : legal) {
    Board child = root;
    child.make_move(move);
    RootMoveInfo cached;
    cached.move = move;
    cached.style_score = evaluate_move_style(position, move, child);
    root_style_cache.push_back(cached);
    ++result.style_evaluations;
  }
  const auto cached_style = [&root_style_cache](const Move& move) {
    const auto it = std::find_if(root_style_cache.begin(), root_style_cache.end(),
        [&move](const RootMoveInfo& info) { return info.move == move; });
    return it->style_score;
  };
  // Keep a small, bounded slice of a timed search for the one final root
  // safety proof.  UCI already has its own transport margin above this.
  const auto objective_deadline = limits.has_deadline
      ? limits.deadline - std::chrono::milliseconds(10) : limits.deadline;
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
                            limits.use_null_move, limits.use_lmr, limits.use_pvs};
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
        return a.score > b.score;
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
    for (RootMoveInfo& info : current) info.style_score = cached_style(info.move);
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
    result.main_nodes = result.nodes - result.qnodes;
    return result;
  }
  auto& current = last_completed;
  const int depth = result.completed_depth;
  const int objective_best = last_objective_best;
  const int threshold = objective_best - AGGRESSION_TOLERANCE_CP;
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
      info.style_safe = info.search_score >= threshold;
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
    if (info.bound == ScoreBound::Upper && info.search_score < threshold) {
      ++result.root_style_rejected;
      continue;
    }
    candidates.push_back(&info);
  }
  std::sort(candidates.begin(), candidates.end(), [](const RootMoveInfo* a, const RootMoveInfo* b) {
    return a->style_score > b->style_score;
  });
  SearchContext verification{result.nodes, result.qnodes, limits.has_deadline,
                             limits.deadline, false, limits.use_tt ? &tt : nullptr, &result,
                             limits.use_see_pruning,
                             limits.use_killer_history ? &search_heuristics() : nullptr,
                             limits.use_null_move, limits.use_lmr, limits.use_pvs};
  for (RootMoveInfo* candidate : candidates) {
    if (candidate->style_score < chosen->style_score) {
      ++result.root_style_prefilter_skips;
      continue;
    }
    ++result.root_style_verification_searches;
    const std::uint64_t before_verification = result.nodes;
    const UndoState undo = root.make_move(candidate->move);
    const int proof = -negamax_impl(root, depth - 1, -threshold, -threshold + 1, 1, verification);
    root.unmake_move(candidate->move, undo);
    result.root_style_verification_nodes += result.nodes - before_verification;
    if (verification.stopped) break;
    candidate->style_safe = proof >= threshold;
    if (candidate->style_safe) {
      ++result.root_style_verified;
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
  if (!verification.stopped && !mate_found) {
    for (RootMoveInfo& info : current) if (info.style_safe) {
      Board child = root;
      child.make_move(info.move);
      info.sacrifice_candidate = is_sacrifice_candidate(position, info.move, child);
      info.see_score = move_see(position, info.move, &result);
    }
    if (root_in_check) {
      for (RootMoveInfo& info : current) {
        if (info.style_safe && is_capture_evasion(info)) {
          chosen = &info;
          break;
        }
      }
    }
    result.best_move = chosen->move;
  } else {
    // A partial final proof never exposes an unverified aggressive move.
    result.best_move = objective_move->move;
  }
  result.score = objective_best;
  result.root_moves = std::move(current);
  result.main_nodes = result.nodes - result.qnodes;
  return result;
}

void clear_transposition_table() noexcept { transposition_table().clear(); }

void clear_search_heuristics() noexcept { search_heuristics().clear(); }

}  // namespace hebichess
