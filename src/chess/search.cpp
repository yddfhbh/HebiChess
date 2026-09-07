#include "chess/search.hpp"

#include <algorithm>

namespace hebichess {
namespace {

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

int negamax(Board& board, int depth, int alpha, int beta, int ply,
            std::uint64_t& nodes) {
  ++nodes;
  std::vector<Move> moves = generate_legal_moves(board);
  if (moves.empty()) {
    const Square king = board.find_king(board.side_to_move());
    return king.is_valid() && board.is_square_attacked(king, opposite(board.side_to_move()))
               ? -MATE_SCORE + ply : 0;
  }
  if (depth <= 0) return evaluate(board);
  std::sort(moves.begin(), moves.end(), [&board](const Move& a, const Move& b) {
    return move_order(board, a) > move_order(board, b);
  });
  int best = -MATE_SCORE;
  for (const Move& move : moves) {
    const UndoState undo = board.make_move(move);
    const int score = -negamax(board, depth - 1, -beta, -alpha, ply + 1, nodes);
    board.unmake_move(move, undo);
    best = std::max(best, score);
    alpha = std::max(alpha, score);
    if (alpha >= beta) break;
  }
  return best;
}

SearchResult search(const Board& position, int max_depth) {
  SearchResult result;
  if (max_depth < 1) return result;
  Board root = position;
  std::vector<Move> legal = generate_legal_moves(root);
  if (legal.empty()) { result.score = negamax(root, 0, -MATE_SCORE, MATE_SCORE, 0, result.nodes); return result; }
  for (int depth = 1; depth <= max_depth; ++depth) {
    std::vector<RootMoveInfo> current;
    int best_score = -MATE_SCORE;
    for (const Move& move : legal) {
      Board child = root;
      child.make_move(move);
      const int score = -negamax(child, depth - 1, -MATE_SCORE, MATE_SCORE, 1, result.nodes);
      current.push_back({move, score, evaluate_move_style(root, move, child),
                         is_sacrifice_candidate(root, move, child)});
      best_score = std::max(best_score, score);
    }
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
    legal = generate_legal_moves(root);
  }
  return result;
}

}  // namespace hebichess
