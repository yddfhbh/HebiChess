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

  const auto bishops = Board::from_fen("4k3/8/8/8/8/8/2BB4/4K3 w - - 0 1").value();
  const auto one_bishop = Board::from_fen("4k3/8/8/8/8/8/2B5/4K3 w - - 0 1").value();
  assert(evaluate_breakdown(bishops, Color::White).bishop_pair >
         evaluate_breakdown(one_bishop, Color::White).bishop_pair);
  const auto isolated = Board::from_fen("4k3/8/8/8/8/8/P7/4K3 w - - 0 1").value();
  const auto connected = Board::from_fen("4k3/8/8/8/8/8/PP6/4K3 w - - 0 1").value();
  assert(evaluate_pawn_structure(isolated, Color::White) <
         evaluate_pawn_structure(connected, Color::White));
  const auto passed = Board::from_fen("4k3/8/8/4P3/8/8/8/4K3 w - - 0 1").value();
  const auto advanced = Board::from_fen("4k3/4P3/8/8/8/8/8/4K3 w - - 0 1").value();
  assert(evaluate_passed_pawns(advanced, Color::White) > evaluate_passed_pawns(passed, Color::White));
  const auto open_rook = Board::from_fen("4k3/8/8/8/8/8/8/R3K3 w - - 0 1").value();
  const auto blocked_rook = Board::from_fen("4k3/8/8/8/8/8/P7/R3K3 w - - 0 1").value();
  assert(evaluate_rooks(open_rook, Color::White) > evaluate_rooks(blocked_rook, Color::White));
  assert(game_phase(Board::initial()) > game_phase(passed));
  const auto attack = Board::from_fen("4k3/3Q4/4N3/8/8/8/8/4K3 w - - 0 1").value();
  assert(evaluate_king_attack(attack, Color::White) > 0);
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
  for (int depth = 1; depth <= 4; ++depth) {
    const SearchResult depth_result = search(mate, depth);
    std::cout << " d" << depth << "=" << depth_result.nodes
              << "/q" << depth_result.qnodes;
  }
  std::cout << '\n';

  const auto hanging_queen = Board::from_fen(
      "3rk3/8/8/8/8/8/3p4/3QK3 w - - 0 1").value();
  const SearchResult hanging_result = search(hanging_queen, 1);
  assert(!has_move(hanging_result, sq('d', 1), sq('d', 2)));
  std::cout << "hanging best="
            << static_cast<char>('a' + hanging_result.best_move.from.file())
            << (hanging_result.best_move.from.rank() + 1)
            << static_cast<char>('a' + hanging_result.best_move.to.file())
            << (hanging_result.best_move.to.rank() + 1)
            << " score=" << hanging_result.score
            << " qnodes=" << hanging_result.qnodes << '\n';

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

void test_quiescence_and_special_tactics() {
  {
    Board board = Board::initial();
    const std::string before = board.to_fen();
    const SearchResult result = search(board, 1);
    assert(result.qnodes > 0);
    assert(result.qnodes <= result.nodes);
    assert(board.to_fen() == before);
  }
  {
    Board board = Board::from_fen(
        "4k3/P7/8/8/8/8/8/4K3 w - - 0 1").value();
    const std::string before = board.to_fen();
    const int score = quiescence(board, -MATE_SCORE, MATE_SCORE, 0);
    assert(score > 500);
    assert(board.to_fen() == before);
  }
  {
    Board board = Board::from_fen(
        "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1").value();
    const std::string before = board.to_fen();
    quiescence(board, -MATE_SCORE, MATE_SCORE, 0);
    assert(board.to_fen() == before);
  }
  {
    Board board = Board::from_fen(
        "7k/6Q1/7K/8/8/8/8/8 b - - 0 1").value();
    assert(quiescence(board, -MATE_SCORE, MATE_SCORE, 0) == -MATE_SCORE);
  }
}

void test_pruning_flags_and_tactics() {
  Board start = Board::initial();
  SearchLimits baseline;
  baseline.max_depth = 4;
  baseline.use_tt = false;
  baseline.use_null_move = false;
  baseline.use_lmr = false;
  SearchLimits optimized = baseline;
  optimized.use_null_move = true;
  optimized.use_lmr = true;
  const std::string fen = start.to_fen();
  const ZobristKey key = start.zobrist_key();
  const SearchResult off = search(start, baseline);
  const SearchResult on = search(start, optimized);
  assert(start.to_fen() == fen);
  assert(start.zobrist_key() == key);
  assert(on.lmr_attempts > 0);
  assert(on.null_attempts > 0);

  const Board king_pawn = Board::from_fen(
      "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1").value();
  SearchLimits zugzwang = optimized;
  const SearchResult zugzwang_result = search(king_pawn, zugzwang);
  assert(zugzwang_result.null_attempts == 0);

  const Board mate = Board::from_fen(
      "7k/5Q2/7K/8/8/8/8/8 w - - 0 1").value();
  const SearchResult mate_result = search(mate, optimized);
  assert(mate_result.score > MATE_SCORE - 10);
  const char* tactical_fens[] = {
      "7k/5Q2/7K/8/8/8/8/8 w - - 0 1", // mate in 1
      "7k/8/6Q1/6K1/8/8/8/8 w - - 0 1", // mate threat
      "3rk3/8/8/8/8/8/3p4/3QK3 w - - 0 1", // hanging queen
      "4k3/8/8/8/3p4/8/3P4/4K3 w - - 0 1", // forced recapture
      "6k1/5ppp/8/8/8/2B5/5PPP/6K1 w - - 0 1", // checking sacrifice candidate
      "4k3/P7/8/8/8/8/8/4K3 w - - 0 1", // promotion tactic
  };
  for (const char* tactical_fen : tactical_fens) {
    const Board tactical = Board::from_fen(tactical_fen).value();
    SearchLimits tactical_limits = optimized;
    tactical_limits.max_depth = 3;
    const SearchResult tactical_result = search(tactical, tactical_limits);
    assert(!generate_legal_moves(tactical).empty());
    assert(tactical_result.best_move.from.is_valid());
  }
  (void)off;
}

void test_pvs_and_aspiration_equivalence() {
  const char* fens[] = {
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      "r2q1rk1/ppp1bppp/2np4/8/2B1P3/2N1BN2/PPP2PPP/R2Q1RK1 w - - 0 1",
      "r1bq1rk1/ppp2ppp/2np4/8/2B1P3/2N1BN2/PPP2PPP/R2Q1RK1 w - - 0 1",
      "7k/5Q2/7K/8/8/8/8/8 w - - 0 1",
  };
  for (const char* fen : fens) {
    const Board board = Board::from_fen(fen).value();
    const std::string before = board.to_fen();
    const ZobristKey key = board.zobrist_key();
    SearchLimits alpha_beta;
    alpha_beta.max_depth = 4;
    alpha_beta.use_pvs = false;
    alpha_beta.use_aspiration = false;
    clear_transposition_table();
    clear_search_heuristics();
    const SearchResult ab = search(board, alpha_beta);

    SearchLimits pvs = alpha_beta;
    pvs.use_pvs = true;
    clear_transposition_table();
    clear_search_heuristics();
    const SearchResult pvs_result = search(board, pvs);
    assert(pvs_result.best_move == ab.best_move);
    assert(pvs_result.score == ab.score);
    assert(pvs_result.pvs_zero_window_searches > 0);

    SearchLimits aspiration = alpha_beta;
    aspiration.use_aspiration = true;
    clear_transposition_table();
    clear_search_heuristics();
    const SearchResult aspiration_result = search(board, aspiration);
    assert(aspiration_result.best_move == ab.best_move);
    assert(aspiration_result.score == ab.score);

    SearchLimits both = pvs;
    both.use_aspiration = true;
    clear_transposition_table();
    clear_search_heuristics();
    const SearchResult both_result = search(board, both);
    assert(both_result.best_move == ab.best_move);
    assert(both_result.score == ab.score);
    assert(board.to_fen() == before);
    assert(board.zobrist_key() == key);
  }
}

}  // namespace

int main() {
  test_evaluation();
  test_search_and_terminal_positions();
  test_style_and_safety_metadata();
  test_quiescence_and_special_tactics();
  test_pruning_flags_and_tactics();
  test_pvs_and_aspiration_equivalence();
}
