#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "chess/search.hpp"
#include "chess/eval.hpp"
#include "chess/nnue.hpp"
#include "chess/nnue_features.hpp"
#include "chess/uci.hpp"

namespace {
using namespace hebichess;

void print_info(int depth, int score, std::uint64_t nodes, std::uint64_t qnodes) {
  if (score > MATE_SCORE - 1000 || score < -MATE_SCORE + 1000) {
    const int distance = std::max(1, (MATE_SCORE - std::abs(score) + 1) / 2);
    std::cout << "info depth " << depth << " score mate "
              << (score >= 0 ? distance : -distance);
  } else {
    std::cout << "info depth " << depth << " score cp " << score;
  }
  std::cout << " nodes " << nodes << " qnodes " << qnodes << std::endl;
}

int integer_after(std::istringstream& input) {
  int value = 0;
  input >> value;
  return std::max(0, value);
}
}  // namespace

int main() {
  using namespace hebichess;
  Board board = Board::initial();
  EvalMode eval_mode = EvalMode::HCE;
  std::string line;
  while (std::getline(std::cin, line)) {
    std::istringstream input(line);
    std::string command;
    input >> command;
    if (command == "uci") {
      std::cout << "id name HebiChess\nid author Hebi\n"
                << "option name EvalMode type combo default HCE var HCE var NNUE\n"
                << "option name EvalFile type string default \n"
                << "uciok" << std::endl;
    } else if (command == "isready") {
      std::cout << "readyok" << std::endl;
    } else if (command == "ucinewgame") {
      board = Board::initial();
      clear_transposition_table();
      clear_search_heuristics();
    } else if (command == "setoption") {
      std::string name_token, name, value_token;
      input >> name_token >> name >> value_token;
      std::string value; std::getline(input, value);
      if (!value.empty() && value.front() == ' ') value.erase(0, 1);
      if (name_token != "name" || value_token != "value") {
        std::cout << "info string error unsupported setoption" << std::endl;
      } else if (name == "EvalFile") {
        std::string error;
        if (value.empty()) { clear_nnue_network(); if (eval_mode == EvalMode::NNUE) eval_mode = EvalMode::HCE; std::cout << "info string NNUE network cleared" << std::endl; }
        else if (load_nnue_network(value, error)) std::cout << "info string NNUE network loaded " << value << std::endl;
        else std::cout << "info string error " << error << std::endl;
      } else if (name == "EvalMode" && value == "HCE") {
        eval_mode = EvalMode::HCE;
        std::cout << "info string EvalMode HCE" << std::endl;
      } else if (name == "EvalMode" && value == "NNUE") {
        if (nnue_network_available()) { eval_mode = EvalMode::NNUE; std::cout << "info string EvalMode NNUE" << std::endl; }
        else std::cout << "info string error EvalMode NNUE unavailable: no network loaded; retaining "
                  << (eval_mode == EvalMode::HCE ? "HCE" : "NNUE") << std::endl;
      } else {
        std::cout << "info string error unsupported setoption" << std::endl;
      }
    } else if (command == "position") {
      std::string kind;
      input >> kind;
      Board next;
      bool valid = false;
      if (kind == "startpos") {
        next = Board::initial();
        valid = true;
      } else if (kind == "fen") {
        std::string fen, field;
        for (int i = 0; i < 6 && input >> field; ++i) {
          if (!fen.empty()) fen += ' ';
          fen += field;
        }
        const auto parsed = Board::from_fen(fen);
        if (parsed) { next = *parsed; valid = true; }
      }
      std::string token;
      input >> token;
      if (valid && token == "moves") {
        while (input >> token) {
          const auto move = parse_uci_move(next, token);
          if (!move) { valid = false; break; }
          next.make_move(*move);
        }
      }
      if (valid) board = next;
    } else if (command == "eval") {
      const EvalBreakdown e = evaluate_breakdown(board, board.side_to_move());
      std::cout << "material " << e.material << "\npst " << e.pst
                << "\nmobility " << e.mobility << "\npawns " << e.pawns
                << "\npassed_pawns " << e.passed_pawns
                << "\nbishop_pair " << e.bishop_pair
                << "\nrook_activity " << e.rook_activity
                << "\nking_safety " << e.king_safety
                << "\nking_attack " << e.king_attack
                << "\nspace " << e.space << "\nthreats " << e.threats
                << "\ninitiative " << e.initiative
                << "\ntotal " << e.total << std::endl;
    } else if (command == "features") {
      // Development helper: `position fen ...` followed by `features`.
      for (Color perspective : {Color::White, Color::Black}) {
        const NnueFeatures features = extract_nnue_features(board, perspective);
        std::cout << (perspective == Color::White ? "white" : "black");
        for (std::size_t i = 0; i < features.size; ++i) std::cout << ' ' << features.indices[i];
        std::cout << '\n';
      }
    } else if (command == "nnueeval") {
      const auto score = evaluate_nnue(board);
      const auto raw_score = evaluate_nnue_network_raw(board);
      if (score && raw_score) std::cout << "nnue " << *score << " raw " << std::setprecision(9)
                                        << *raw_score << std::endl;
      else std::cout << "info string error NNUE unavailable" << std::endl;
    } else if (command == "go") {
      SearchLimits limits;
      limits.max_depth = 64;
      limits.eval_mode = eval_mode;
      bool has_movetime = false;
      int movetime = 0, wtime = 0, btime = 0, winc = 0, binc = 0;
      std::string option;
      while (input >> option) {
        if (option == "depth") limits.max_depth = std::max(1, integer_after(input));
        else if (option == "movetime") { movetime = integer_after(input); has_movetime = true; }
        else if (option == "wtime") wtime = integer_after(input);
        else if (option == "btime") btime = integer_after(input);
        else if (option == "winc") winc = integer_after(input);
        else if (option == "binc") binc = integer_after(input);
      }
      if (has_movetime || wtime > 0 || btime > 0) {
        int budget = movetime;
        if (!has_movetime) {
          const int remaining = board.side_to_move() == Color::White ? wtime : btime;
          const int increment = board.side_to_move() == Color::White ? winc : binc;
          budget = remaining / 30 + increment / 2;
          budget = std::min(budget, std::max(1, remaining - 20));
        }
        budget = std::max(1, budget - (has_movetime ? std::min(20, budget / 10) : 10));
        limits.has_deadline = true;
        limits.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budget);
      }
      const SearchResult result = search(board, limits, print_info);
      std::cout << "info string nodes " << result.nodes
                << " main_nodes " << result.main_nodes
                << " qnodes " << result.qnodes
                << " qdelta_prunes " << result.qdelta_prunes
                << " tt probes " << result.tt_probes
                << " hits " << result.tt_hits << " cutoffs "
                << result.tt_cutoffs << " see_calls " << result.see_calls
                << " see_prunes " << result.see_prunes
                << " killer_cutoffs " << result.killer_cutoffs
                << " killer_uses " << result.killer_uses
                << " history_cutoffs " << result.history_cutoffs
                << " null_attempts " << result.null_attempts
                << " null_cutoffs " << result.null_cutoffs
                << " lmr_attempts " << result.lmr_attempts
                << " lmr_researches " << result.lmr_researches
                << " lmr_reduced_nodes " << result.lmr_reduced_search_nodes
                << " lmr_research_nodes " << result.lmr_research_nodes
                << " pvs_zero_window_searches " << result.pvs_zero_window_searches
                << " pvs_researches " << result.pvs_researches
                << " pvs_research_nodes " << result.pvs_research_nodes
                << " root_style_candidates " << result.root_style_candidates
                << " root_style_prefilter_skips " << result.root_style_prefilter_skips
                << " root_style_verifications " << result.root_style_verification_searches
                << " root_style_nodes " << result.root_style_verification_nodes
                << " root_style_verified " << result.root_style_verified
                << " root_style_rejected " << result.root_style_rejected
                << " style_evaluations " << result.style_evaluations
                << " aspiration_retries " << result.aspiration_retries
                << " aspiration_fail_highs " << result.aspiration_fail_highs
                << " aspiration_fail_lows " << result.aspiration_fail_lows << std::endl;
      std::cout << "bestmove " << move_to_uci(result.best_move) << std::endl;
    } else if (command == "quit") {
      break;
    }
  }
}
