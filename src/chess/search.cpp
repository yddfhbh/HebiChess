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

int move_order(Board& board, const Move& move, SearchResult* result = nullptr,
               SearchHeuristics* heuristics = nullptr, int ply = 0) noexcept {
  const int see = move_see(board, move, result);
  const bool capture = is_capture(move);
  int score = 0;
  const Piece victim = board.piece_at(move.to);
  if (capture && see >= 0) score += 4000000 + see * 10;
  else if (move.is_promotion()) score += 3000000 + see * 10;
  else if (is_quiet_move(move) && heuristics != nullptr &&
           ply >= 0 && ply < MAX_SEARCH_PLY) {
    if (move == heuristics->killers[ply][0]) {
      score += 2500000;
      if (result != nullptr) ++result->killer_uses;
    } else if (move == heuristics->killers[ply][1]) {
      score += 2400000;
      if (result != nullptr) ++result->killer_uses;
    } else if (heuristics->history_score(board.side_to_move(), move) > 0) {
      score += 2200000 + heuristics->history_score(board.side_to_move(), move);
    } else if (gives_check(board, move)) score += 2000000;
  }
  else if (gives_check(board, move)) score += 2000000;
  else if (capture) score += 1000000 + see * 10;
  if (move.flag == MoveFlag::EnPassant) score += 10 * piece_value(PieceType::Pawn);
  else if (!victim.is_empty()) score += 10 * piece_value(victim.type) - piece_value(board.piece_at(move.from).type);
  return score;
}

bool is_tactical_move(const Move& move) noexcept {
  return move.flag == MoveFlag::Capture || move.flag == MoveFlag::EnPassant ||
         move.flag == MoveFlag::Promotion ||
         move.flag == MoveFlag::PromotionCapture;
}

int quiescence_move_order(Board& board, const Move& move, SearchResult* result) noexcept {
  const bool capture = is_capture(move);
  const bool promotion = move.is_promotion();
  const bool en_passant = move.flag == MoveFlag::EnPassant;
  const int see = move_see(board, move, result);
  int score = capture && see >= 0 ? 4000000 + see * 10 :
              promotion ? 3000000 + see * 10 :
              capture ? 1000000 + see * 10 : en_passant ? 50000 : 0;
  if (capture) {
    const Piece victim = en_passant
        ? Piece{PieceType::Pawn, opposite(board.side_to_move())}
        : board.piece_at(move.to);
    score += 10 * piece_value(victim.type) -
             piece_value(board.piece_at(move.from).type);
  }
  return score;
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
  const int escape_delta = count_king_escapes(after, opposite(mover)) -
                           count_king_escapes(before, opposite(mover));
  int style = (check ? 18 : 0) + std::max(0, pressure_delta) * 2 +
              std::max(0, -escape_delta) * 3 + (move.is_promotion() ? 12 : 0);
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
  return check || pressure_delta >= 5 || escape_delta <= -2;
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
  const std::vector<Move> legal = generate_legal_moves(board);

  if (in_check) {
    if (legal.empty()) return -MATE_SCORE + ply;
    std::vector<Move> evasions = legal;
    std::sort(evasions.begin(), evasions.end(), [&board, &context, ply](const Move& a, const Move& b) {
      return move_order(board, a, context.result, context.heuristics, ply) >
             move_order(board, b, context.result, context.heuristics, ply);
    });
    int best = -MATE_SCORE;
    for (const Move& move : evasions) {
      const UndoState undo = board.make_move(move);
      const int score = -quiescence_impl(board, -beta, -alpha, ply + 1,
                                         context);
      board.unmake_move(move, undo);
      if (context.stopped) return 0;
      best = std::max(best, score);
      alpha = std::max(alpha, score);
      if (alpha >= beta) break;
    }
    return best;
  }

  if (legal.empty()) return 0;
  const int stand_pat = evaluate(board);
  if (stand_pat >= beta) return beta;
  alpha = std::max(alpha, stand_pat);

  std::vector<Move> tactical;
  for (const Move& move : legal)
    if (is_tactical_move(move)) tactical.push_back(move);
  std::sort(tactical.begin(), tactical.end(), [&board, &context](const Move& a, const Move& b) {
      return quiescence_move_order(board, a, context.result) >
             quiescence_move_order(board, b, context.result);
  });
  for (const Move& move : tactical) {
    const bool promotion = move.is_promotion();
    const bool capture = is_capture(move);
    const bool check = gives_check(board, move);
    const int see = move_see(board, move, context.result);
    if (context.use_see_pruning && capture && see < -100 && !check && !promotion) {
      if (context.result != nullptr) ++context.result->see_prunes;
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
                 SearchContext& context) {
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
  std::vector<Move> moves = generate_legal_moves(board);
  if (moves.empty()) {
    const Square king = board.find_king(board.side_to_move());
    return king.is_valid() && board.is_square_attacked(king, opposite(board.side_to_move()))
               ? -MATE_SCORE + ply : 0;
  }
  int best = -MATE_SCORE;
  std::optional<Move> best_move;
  std::sort(moves.begin(), moves.end(), [&board, &tt_move, &context, ply](const Move& a, const Move& b) {
    const bool a_is_tt = tt_move.has_value() && a == *tt_move;
    const bool b_is_tt = tt_move.has_value() && b == *tt_move;
    if (a_is_tt != b_is_tt) return a_is_tt;
      return move_order(board, a, context.result, context.heuristics, ply) >
             move_order(board, b, context.result, context.heuristics, ply);
  });
  for (const Move& move : moves) {
    const UndoState undo = board.make_move(move);
    const int score = -negamax_impl(board, depth - 1, -beta, -alpha, ply + 1,
                                    context);
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
                          limits.use_killer_history ? &search_heuristics() : nullptr};
    result.score = negamax_impl(root, 0, -MATE_SCORE, MATE_SCORE, 0, context);
    return result;
  }
  result.best_move = legal.front();
  for (int depth = 1; depth <= limits.max_depth; ++depth) {
    SearchContext context{result.nodes, result.qnodes, limits.has_deadline,
                          limits.deadline, false,
                          limits.use_tt ? &tt : nullptr, &result,
                          limits.use_see_pruning,
                          limits.use_killer_history ? &search_heuristics() : nullptr};
    std::vector<RootMoveInfo> current;
    int best_score = -MATE_SCORE;
    for (const Move& move : legal) {
      if (context.should_stop()) break;
      const UndoState undo = root.make_move(move);
      const int score = -negamax_impl(root, depth - 1, -MATE_SCORE, MATE_SCORE,
                                      1, context);
      Board child = root;
      root.unmake_move(move, undo);
      if (context.stopped) break;
      child.make_move(move);
      current.push_back({move, score, evaluate_move_style(position, move, child),
                         is_sacrifice_candidate(position, move, child),
                         move_see(position, move, &result)});
      best_score = std::max(best_score, score);
    }
    if (context.stopped || current.size() != legal.size()) break;
    const bool mate_found = best_score > MATE_SCORE - 1000 || best_score < -MATE_SCORE + 1000;
    auto chosen = current.begin();
    for (auto candidate = std::next(current.begin()); candidate != current.end(); ++candidate) {
      const bool chosen_ok = chosen->search_score >= best_score - AGGRESSION_TOLERANCE_CP;
      const bool candidate_ok = candidate->search_score >= best_score - AGGRESSION_TOLERANCE_CP;
      bool candidate_wins = false;
      if (mate_found || !chosen_ok || !candidate_ok) {
        candidate_wins = candidate->search_score > chosen->search_score;
      } else if (candidate->style_score != chosen->style_score) {
        candidate_wins = candidate->style_score > chosen->style_score;
      } else {
        candidate_wins = candidate->search_score > chosen->search_score;
      }
      if (candidate_wins) chosen = candidate;
    }
    result.best_move = chosen->move;
    result.score = chosen->search_score;
    result.root_moves = std::move(current);
    if (on_iteration) on_iteration(depth, result.score, result.nodes, result.qnodes);
    legal = generate_legal_moves(root);
  }
  return result;
}

void clear_transposition_table() noexcept { transposition_table().clear(); }

void clear_search_heuristics() noexcept { search_heuristics().clear(); }

}  // namespace hebichess
