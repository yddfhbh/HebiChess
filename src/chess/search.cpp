#include "chess/search.hpp"

#include <algorithm>
#include <chrono>

namespace hebichess {
namespace {

struct SearchContext {
  std::uint64_t& nodes;
  std::uint64_t& qnodes;
  bool has_deadline{false};
  std::chrono::steady_clock::time_point deadline{};
  bool stopped{false};

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

int move_order(Board& board, const Move& move) noexcept {
  int score = move.is_promotion() ? 100000 : 0;
  const Piece victim = board.piece_at(move.to);
  if (move.flag == MoveFlag::EnPassant) score += 1000 + piece_value(PieceType::Pawn);
  else if (!victim.is_empty()) score += 1000 + 10 * piece_value(victim.type) - piece_value(board.piece_at(move.from).type);
  if (gives_check(board, move)) score += 500;
  return score;
}

bool is_tactical_move(const Move& move) noexcept {
  return move.flag == MoveFlag::Capture || move.flag == MoveFlag::EnPassant ||
         move.flag == MoveFlag::Promotion ||
         move.flag == MoveFlag::PromotionCapture;
}

int quiescence_move_order(const Board& board, const Move& move) noexcept {
  const bool promotion_capture = move.flag == MoveFlag::PromotionCapture;
  const bool capture = move.flag == MoveFlag::Capture || promotion_capture;
  const bool promotion = move.is_promotion();
  const bool en_passant = move.flag == MoveFlag::EnPassant;
  int score = promotion_capture ? 300000 : capture ? 200000 :
              promotion ? 100000 : en_passant ? 50000 : 0;
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
    std::sort(evasions.begin(), evasions.end(), [&board](const Move& a, const Move& b) {
      return move_order(board, a) > move_order(board, b);
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
  std::sort(tactical.begin(), tactical.end(), [&board](const Move& a, const Move& b) {
    return quiescence_move_order(board, a) > quiescence_move_order(board, b);
  });
  for (const Move& move : tactical) {
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
  ++context.nodes;
  std::vector<Move> moves = generate_legal_moves(board);
  if (moves.empty()) {
    const Square king = board.find_king(board.side_to_move());
    return king.is_valid() && board.is_square_attacked(king, opposite(board.side_to_move()))
               ? -MATE_SCORE + ply : 0;
  }
  std::sort(moves.begin(), moves.end(), [&board](const Move& a, const Move& b) {
    return move_order(board, a) > move_order(board, b);
  });
  int best = -MATE_SCORE;
  for (const Move& move : moves) {
    const UndoState undo = board.make_move(move);
    const int score = -negamax_impl(board, depth - 1, -beta, -alpha, ply + 1,
                                    context);
    board.unmake_move(move, undo);
    if (context.stopped) return 0;
    best = std::max(best, score);
    alpha = std::max(alpha, score);
    if (alpha >= beta) break;
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
  std::vector<Move> legal = generate_legal_moves(root);
  if (legal.empty()) {
    SearchContext context{result.nodes, result.qnodes, limits.has_deadline,
                          limits.deadline};
    result.score = negamax_impl(root, 0, -MATE_SCORE, MATE_SCORE, 0, context);
    return result;
  }
  result.best_move = legal.front();
  for (int depth = 1; depth <= limits.max_depth; ++depth) {
    SearchContext context{result.nodes, result.qnodes, limits.has_deadline,
                          limits.deadline};
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
                         is_sacrifice_candidate(position, move, child)});
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

}  // namespace hebichess
