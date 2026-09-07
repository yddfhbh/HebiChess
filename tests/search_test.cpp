#include <cassert>
#include <iostream>
#include <string>

#include "chess/eval.hpp"
#include "chess/search.hpp"

using namespace hebichess;

namespace {

Square sq(char file, int rank) {
  return Square::from_file_rank(static_cast<std::uint8_t>(file - 'a'),
                                static_cast<std::uint8_t>(rank - 1));
}

bool has_move(const SearchResult& result, Square from, Square to) {
  return result.best_move.from == from && result.best_move.to == to;
}

void test_evaluation() {
  const auto equal = Board::from_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1").value();
  assert(evaluate_material(equal, Color::White) == 0);
  const auto queen_up = Board::from_fen("4k3/8/8/8/8/8/4Q3/4K3 w - - 0 1").value();
  assert(evaluate_material(queen_up, Color::White) == 900);
  assert(evaluate_material(queen_up, Color::Black) == -900);
  assert(evaluate(queen_up) > 800);
  Board black_to_move = queen_up;
  black_to_move.set_side_to_move(Color::Black);
  assert(evaluate(black_to_move) < -800);
}

void test_search_and_terminal_positions() {
  const auto mate = Board::from_fen("7k/5Q2/7K/8/8/8/8/8 w - - 0 1").value();
  const SearchResult mate_result = search(mate, 2);
  assert(mate_result.score > MATE_SCORE - 10);
  assert(mate_result.nodes > 0);
  std::cout << "example best=" << static_cast<char>('a' + mate_result.best_move.from.file())
            << (mate_result.best_move.from.rank() + 1) << static_cast<char>('a' + mate_result.best_move.to.file())
            << (mate_result.best_move.to.rank() + 1) << " search=" << mate_result.score;
  for (const RootMoveInfo& info : mate_result.root_moves)
    if (info.move == mate_result.best_move) std::cout << " style=" << info.style_score;
  std::cout << '\n';
  std::cout << "nodes";
  for (int depth = 1; depth <= 4; ++depth) std::cout << " d" << depth << "=" << search(mate, depth).nodes;
  std::cout << '\n';

  const auto stalemate = Board::from_fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1").value();
  assert(search(stalemate, 1).score == 0);

  const auto queen_capture = Board::from_fen("6k1/8/8/8/8/8/3q4/3QK3 w - - 0 1").value();
  const std::string before = queen_capture.to_fen();
  const SearchResult capture_result = search(queen_capture, 1);
  assert(has_move(capture_result, sq('d', 1), sq('d', 2)));
  assert(queen_capture.to_fen() == before);
}

void test_style_and_safety_metadata() {
  const auto board = Board::from_fen("4k3/8/8/8/8/8/4Q3/4K3 w - - 0 1").value();
  const SearchResult result = search(board, 1);
  assert(!result.root_moves.empty());
  for (const RootMoveInfo& info : result.root_moves)
    assert(info.search_score > -MATE_SCORE);
  assert(AGGRESSION_TOLERANCE_CP == 35);
}

}  // namespace

int main() {
  test_evaluation();
  test_search_and_terminal_positions();
  test_style_and_safety_metadata();
}
