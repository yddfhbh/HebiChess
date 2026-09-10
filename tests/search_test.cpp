#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

#include "chess/eval.hpp"
#include "chess/search.hpp"
#include "chess/see.hpp"

using namespace hebichess;

namespace {

Square sq(char file, int rank) {
  return Square::from_file_rank(static_cast<std::uint8_t>(file - 'a'),
                                static_cast<std::uint8_t>(rank - 1));
}

bool has_move(const SearchResult& result, Square from, Square to) {
  return result.best_move.from == from && result.best_move.to == to;
}

Move legal_move(const Board& board, const char* uci) {
  for (const Move& move : generate_legal_moves(board)) {
    if (move.from == sq(uci[0], uci[1] - '0') && move.to == sq(uci[2], uci[3] - '0'))
      return move;
  }
  assert(false && "test move must be legal");
  return {};
}

int style_for(Board& board, const char* uci) {
  const Move move = legal_move(board, uci);
  const std::string before = board.to_fen();
  const Board position = board;
  const UndoState undo = board.make_move(move);
  const int style = evaluate_move_style(position, move, board);
  board.unmake_move(move, undo);
  assert(board.to_fen() == before);
  return style;
}

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "search regression failure: " << message << '\n';
    std::abort();
  }
}

void play(Board& board, const char* uci) {
  const auto legal = generate_legal_moves(board);
  for (const Move& move : legal) {
    if (move.from == sq(uci[0], uci[1] - '0') &&
        move.to == sq(uci[2], uci[3] - '0')) {
      board.make_move(move);
      return;
    }
  }
  assert(false && "test move must be legal");
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

void test_hce_backend_regression() {
  struct RegressionCase {
    const char* name;
    const char* fen;
    int expected_hce;
  };
  // Captured before the backend split.  These cover initial, opening,
  // middlegame, endgame, and the Qe5 tactical position in both turn states.
  constexpr RegressionCase cases[] = {
      {"startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 10},
      {"opening", "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3", 5},
      {"middlegame", "r1bq1rk1/pp2bppp/2n1pn2/2bp4/8/1PNP1NP1/PBPPPPBP/R2Q1RK1 w - - 0 8", -131},
      {"endgame", "8/8/3k4/8/3K4/8/4P3/8 w - - 0 1", 119},
      {"qe5-black", "r1b1kb1r/ppp1pppp/2nq3n/8/3P4/5N2/PPP2PPP/RNBQKB1R b KQkq - 0 5", 56},
      {"qe5-white", "r1b1kb1r/ppp1pppp/2nq3n/8/3P4/5N2/PPP2PPP/RNBQKB1R w KQkq - 0 5", -36},
  };
  for (const RegressionCase& test : cases) {
    const auto board = Board::from_fen(test.fen);
    require(board.has_value(), test.name);
    require(evaluate_hce(*board) == test.expected_hce, test.name);
    require(evaluate(*board) == test.expected_hce, test.name);
    const std::optional<int> selected = evaluate(*board, EvalMode::HCE);
    require(selected.has_value() && *selected == test.expected_hce, test.name);
  }
  require(eval_mode_available(EvalMode::HCE), "HCE must be available");
  require(!eval_mode_available(EvalMode::NNUE), "NNUE must be unavailable without a network");
  require(!evaluate_nnue(Board::initial()).has_value(), "NNUE must not synthesize a score");
  require(!evaluate(Board::initial(), EvalMode::NNUE).has_value(),
          "NNUE mode must report unavailable");
}

void test_opening_development_and_breakdown() {
  Board opening = Board::initial();
  play(opening, "e2e4");
  play(opening, "d7d5");
  play(opening, "d2d4");
  play(opening, "d5e4");
  play(opening, "b1c3");
  assert(opening.side_to_move() == Color::Black);

  const auto candidate = [&](const char* uci) {
    Board after = opening;
    play(after, uci);
    return evaluate_breakdown(after, Color::Black);
  };
  const EvalBreakdown nf6 = candidate("g8f6");
  const EvalBreakdown nc6 = candidate("b8c6");
  const EvalBreakdown a6 = candidate("a7a6");
  const EvalBreakdown h6 = candidate("h7h6");
  const EvalBreakdown g5 = candidate("g7g5");
  std::cout << "opening black candidates (total/development/space/attack):\n"
            << "  Nf6 " << nf6.total << '/' << nf6.development << '/' << nf6.space << '/' << nf6.king_attack << '\n'
            << "  Nc6 " << nc6.total << '/' << nc6.development << '/' << nc6.space << '/' << nc6.king_attack << '\n'
            << "  a6  " << a6.total << '/' << a6.development << '/' << a6.space << '/' << a6.king_attack << '\n'
            << "  h6  " << h6.total << '/' << h6.development << '/' << h6.space << '/' << h6.king_attack << '\n'
            << "  g5  " << g5.total << '/' << g5.development << '/' << g5.space << '/' << g5.king_attack << '\n';
  assert(nf6.development > a6.development);
  assert(nc6.development > h6.development);

  const SearchResult result = search(opening, 2);
  std::cout << "opening best="
            << static_cast<char>('a' + result.best_move.from.file())
            << (result.best_move.from.rank() + 1)
            << static_cast<char>('a' + result.best_move.to.file())
            << (result.best_move.to.rank() + 1) << '\n';
  for (const RootMoveInfo& info : result.root_moves) {
    std::cout << "  root " << static_cast<char>('a' + info.move.from.file())
              << (info.move.from.rank() + 1)
              << static_cast<char>('a' + info.move.to.file())
              << (info.move.to.rank() + 1) << " score=" << info.search_score
              << " style=" << info.style_score << '\n';
  }

  Board replay = Board::initial();
  for (const char* move : {"e2e4", "d7d5", "d2d4", "d5e4", "b1c3", "a7a6",
                           "c1f4", "a6a5", "f2f3", "a5a4", "f3e4", "h7h6",
                           "g1f3", "h6h5", "f4e3", "h5h4"}) {
    play(replay, move);
  }
  const EvalBreakdown repeated_flank = evaluate_breakdown(replay, Color::Black);
  std::cout << "replayed flank line: total=" << repeated_flank.total
            << " pawns=" << repeated_flank.pawns
            << " development=" << repeated_flank.development
            << " space=" << repeated_flank.space << '\n';
  assert(repeated_flank.development < 0);
  assert(repeated_flank.pawns < 0);
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

void test_aggressive_style_v2() {
  // Development is a useful first attacking step; a/h pawn moves are not.
  Board opening = Board::initial();
  const int development = style_for(opening, "g1f3");
  const int flank_pawn = style_for(opening, "a2a3");
  require(development > flank_pawn,
          "opening development must outrank irrelevant flank pawn spam");

  // Nf5 increases control of g7 around the black king and joins the attack.
  Board preparation = Board::from_fen(
      "6k1/5ppp/8/8/2BN4/8/5PPP/3Q1RK1 w - - 0 1").value();
  const std::string preparation_fen = preparation.to_fen();
  const int attack_prep = style_for(preparation, "d4f5");
  const int retreat = style_for(preparation, "d4b3");
  require(attack_prep > retreat,
          "quiet attacking preparation must outrank an irrelevant retreat");
  require(preparation.to_fen() == preparation_fen,
          "style evaluation must restore the preparation board");

  Board ring_control = preparation;
  const Move nf5 = legal_move(ring_control, "d4f5");
  const Move nb3 = legal_move(ring_control, "d4b3");
  ring_control.make_move(nf5);
  const int nf5_style = evaluate_move_style(preparation, nf5, ring_control);
  // Rebuild the comparison child so every diagnostic has an explicit move.
  Board retreat_child = preparation;
  retreat_child.make_move(nb3);
  const int nb3_style = evaluate_move_style(preparation, nb3, retreat_child);
  require(nf5_style > nb3_style,
          "increased king-ring control must increase style score");

  // A quiet queen sortie cannot use two cheap static signals to skip four
  // undeveloped minor pieces.  This must remain a style-layer preference, not
  // an objective-search or FEN-specific rule.
  Board premature_queen = Board::from_fen(
      "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2").value();
  const int qh5 = style_for(premature_queen, "d1h5");
  const int bb5 = style_for(premature_queen, "f1b5");
  const int bc4 = style_for(premature_queen, "f1c4");
  const int nf3 = style_for(premature_queen, "g1f3");
  require(qh5 < std::max({bb5, bc4, nf3}),
          "premature Qh5 must rank below a normal developing move");
  clear_transposition_table();
  clear_search_heuristics();
  SearchLimits early_queen_search;
  early_queen_search.max_depth = 4;
  early_queen_search.use_pvs = true;
  early_queen_search.use_aspiration = true;
  const SearchResult early_queen_result = search(premature_queen, early_queen_search);
  require(early_queen_result.best_move != Move{sq('d', 1), sq('h', 5)},
          "depth-4 style selector must not choose premature Qh5");

  // Once all original minor squares are clear, a queen may still join a real
  // king-side attack rather than being treated as an early sortie.
  Board developed_attack = Board::from_fen(
      "r2q1rk1/ppp2pp1/2nppn1p/2b1p1N1/2B1P3/2NPB3/PPP2PPP/R2Q1RK1 w - - 0 9").value();
  const int qh5_developed = style_for(developed_attack, "d1h5");
  const int queen_retreat = style_for(developed_attack, "d1d2");
  require(qh5_developed > queen_retreat,
          "developed queen attack preparation must outrank an irrelevant retreat");

  Board sacrifice = Board::from_fen(
      "6k1/5ppp/8/8/8/3B4/5PPP/3Q2K1 w - - 0 1").value();
  const Move bxh7 = legal_move(sacrifice, "d3h7");
  require(static_exchange_eval(sacrifice, bxh7) < 0,
          "Bxh7+ must be a negative SEE capture sacrifice");
  Board sacrifice_after = sacrifice;
  sacrifice_after.make_move(bxh7);
  require(is_sacrifice_candidate(sacrifice, bxh7, sacrifice_after),
          "negative SEE checking sacrifice must be recognized");
  std::cout << "style v2.1: Nf5=" << attack_prep << " Nb3=" << retreat
            << " early_Qh5=" << qh5 << " Bb5=" << bb5 << " Bc4=" << bc4
            << " Nf3=" << nf3 << " developed_Qh5=" << qh5_developed
            << " retreat=" << queen_retreat
            << " Bxh7+ SEE=" << static_exchange_eval(sacrifice, bxh7)
            << " sacrifice=" << is_sacrifice_candidate(sacrifice, bxh7, sacrifice_after)
            << '\n';
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
    // Iterative root ordering intentionally changes the PV explored first.
    // Each mode must return a legal, completed root result.
    assert(pvs_result.best_move.from.is_valid());
    assert(pvs_result.pvs_zero_window_searches > 0);

    SearchLimits aspiration = alpha_beta;
    aspiration.use_aspiration = true;
    clear_transposition_table();
    clear_search_heuristics();
    const SearchResult aspiration_result = search(board, aspiration);
    assert(aspiration_result.best_move.from.is_valid());

    SearchLimits both = pvs;
    both.use_aspiration = true;
    clear_transposition_table();
    clear_search_heuristics();
    const SearchResult both_result = search(board, both);
    assert(both_result.best_move.from.is_valid());
    assert(board.to_fen() == before);
    assert(board.zobrist_key() == key);
  }
}

void test_qe5_hanging_queen_regression() {
  Board board = Board::initial();
  for (const char* move : {"e2e4", "b8c6", "d2d4", "g8h6", "e4e5", "d7d6",
                           "e5d6", "d8d6", "g1f3"}) {
    play(board, move);
  }
  const Move qe5{sq('d', 6), sq('e', 5)};
  for (int depth = 1; depth <= 4; ++depth) {
    clear_transposition_table();
    clear_search_heuristics();
    SearchLimits limits;
    limits.max_depth = depth;
    limits.use_pvs = true;
    limits.use_aspiration = true;
    const SearchResult result = search(board, limits);
    require(result.best_move != qe5, "Qd6-e5+ must never be selected");
    const auto qe5_info = std::find_if(result.root_moves.begin(), result.root_moves.end(),
        [&qe5](const RootMoveInfo& info) { return info.move == qe5; });
    require(qe5_info != result.root_moves.end(), "Qd6-e5+ must be a root candidate");
    require(!qe5_info->style_safe,
            "Qd6-e5+ must never pass the aggression threshold proof");
    require(qe5_info->style_score >= 20,
            "Qd6-e5+ remains a high-style move but must not bypass safety");
    require(qe5_info->bound == ScoreBound::Upper ||
                qe5_info->search_score < result.score - AGGRESSION_TOLERANCE_CP,
            "hanging queen must fail the objective safety gate");
    if (qe5_info->bound == ScoreBound::Exact) {
      require(qe5_info->search_score < result.score - 300,
              "Qd6-e5+ must score far below the safe objective move");
    }
    std::cout << "Qe5 regression d" << depth << " best="
              << static_cast<char>('a' + result.best_move.from.file())
              << (result.best_move.from.rank() + 1)
              << static_cast<char>('a' + result.best_move.to.file())
              << (result.best_move.to.rank() + 1)
              << " best_score=" << result.score
              << " qe5_score=" << qe5_info->search_score
              << " qe5_style=" << qe5_info->style_score
              << " qe5_safe=" << qe5_info->style_safe
              << " qe5_sacrifice=" << qe5_info->sacrifice_candidate << '\n';
  }

  clear_transposition_table();
  clear_search_heuristics();
  SearchLimits timed;
  timed.max_depth = 64;
  timed.has_deadline = true;
  timed.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
  const SearchResult timed_result = search(board, timed);
  require(timed_result.completed_depth > 0,
          "timed Qe5 search must retain a completed iteration");
  require(timed_result.best_move != qe5,
          "timed Qe5 search must not return Qd6-e5+");
  const auto timed_qe5 = std::find_if(timed_result.root_moves.begin(),
      timed_result.root_moves.end(), [&qe5](const RootMoveInfo& info) {
        return info.move == qe5;
      });
  require(timed_qe5 != timed_result.root_moves.end(),
          "timed Qd6-e5+ must be a root candidate");
  require(!timed_qe5->style_safe,
          "timed Qd6-e5+ must fail the aggression threshold proof");
}

}  // namespace

int main() {
  test_evaluation();
  test_hce_backend_regression();
  test_opening_development_and_breakdown();
  test_search_and_terminal_positions();
  test_style_and_safety_metadata();
  test_aggressive_style_v2();
  test_quiescence_and_special_tactics();
  test_pruning_flags_and_tactics();
  test_pvs_and_aspiration_equivalence();
  test_qe5_hanging_queen_regression();
}
