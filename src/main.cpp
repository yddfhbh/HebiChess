#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

#include "chess/search.hpp"
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
  std::string line;
  while (std::getline(std::cin, line)) {
    std::istringstream input(line);
    std::string command;
    input >> command;
    if (command == "uci") {
      std::cout << "id name HebiChess\nid author Hebi\nuciok" << std::endl;
    } else if (command == "isready") {
      std::cout << "readyok" << std::endl;
    } else if (command == "ucinewgame") {
      board = Board::initial();
      clear_transposition_table();
      clear_search_heuristics();
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
    } else if (command == "go") {
      SearchLimits limits;
      limits.max_depth = 64;
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
      std::cout << "info string tt probes " << result.tt_probes
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
                << " pvs_zero_window_searches " << result.pvs_zero_window_searches
                << " pvs_researches " << result.pvs_researches
                << " aspiration_retries " << result.aspiration_retries
                << " aspiration_fail_highs " << result.aspiration_fail_highs
                << " aspiration_fail_lows " << result.aspiration_fail_lows << std::endl;
      std::cout << "bestmove " << move_to_uci(result.best_move) << std::endl;
    } else if (command == "quit") {
      break;
    }
  }
}
