#include "chess/uci_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <sstream>

#include "chess/nnue.hpp"
#include "chess/nnue_features.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"

namespace hebichess {
namespace {

int integer_after(std::istringstream& input) {
  int value = 0;
  input >> value;
  return std::max(0, value);
}

}  // namespace

UciEngine::UciEngine(Output output) : output_(std::move(output)) {}

void UciEngine::send_command(const std::string& line) {
  std::istringstream input(line);
  std::string command;
  input >> command;
  const auto emit = [this](const std::string& value) { output_(value); };
  if (command == "uci") {
    emit("id name HebiChess");
    emit("id author Hebi");
#ifdef HEBICHESS_WASM
    emit("option name EvalMode type combo default HCE var HCE");
#else
    emit("option name EvalMode type combo default HCE var HCE var NNUE");
    emit("option name EvalFile type string default ");
#endif
    emit("uciok");
  } else if (command == "isready") {
    emit("readyok");
  } else if (command == "ucinewgame") {
    board_ = Board::initial();
    clear_transposition_table();
    clear_search_heuristics();
  } else if (command == "setoption") {
    std::string name_token, name, value_token;
    input >> name_token >> name >> value_token;
    std::string value;
    std::getline(input, value);
    if (!value.empty() && value.front() == ' ') value.erase(0, 1);
    if (name_token != "name" || value_token != "value") {
      emit("info string error unsupported setoption");
#ifdef HEBICHESS_WASM
    } else if (name == "EvalMode" && value == "HCE") {
      eval_mode_ = EvalMode::HCE;
      emit("info string EvalMode HCE");
    } else {
      emit("info string error WASM build supports HCE only");
    }
#else
    } else if (name == "EvalFile") {
      std::string error;
      if (value.empty()) {
        clear_nnue_network();
        if (eval_mode_ == EvalMode::NNUE) eval_mode_ = EvalMode::HCE;
        emit("info string NNUE network cleared");
      } else if (load_nnue_network(value, error)) {
        emit("info string NNUE network loaded " + value);
      } else {
        emit("info string error " + error);
      }
    } else if (name == "EvalMode" && value == "HCE") {
      eval_mode_ = EvalMode::HCE;
      emit("info string EvalMode HCE");
    } else if (name == "EvalMode" && value == "NNUE") {
      if (nnue_network_available()) {
        eval_mode_ = EvalMode::NNUE;
        emit("info string EvalMode NNUE");
      } else {
        emit("info string error EvalMode NNUE unavailable: no network loaded; retaining " +
             std::string(eval_mode_ == EvalMode::HCE ? "HCE" : "NNUE"));
      }
    } else {
      emit("info string error unsupported setoption");
    }
#endif
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
    if (valid) board_ = next;
    else emit("info string error invalid position");
  } else if (command == "eval") {
    const EvalBreakdown e = evaluate_breakdown(board_, board_.side_to_move());
    emit("info string eval material " + std::to_string(e.material));
    emit("info string eval pst " + std::to_string(e.pst));
    emit("info string eval mobility " + std::to_string(e.mobility));
    emit("info string eval pawns " + std::to_string(e.pawns));
    emit("info string eval passed_pawns " + std::to_string(e.passed_pawns));
    emit("info string eval bishop_pair " + std::to_string(e.bishop_pair));
    emit("info string eval rook_activity " + std::to_string(e.rook_activity));
    emit("info string eval king_safety " + std::to_string(e.king_safety));
    emit("info string eval king_attack " + std::to_string(e.king_attack));
    emit("info string eval space " + std::to_string(e.space));
    emit("info string eval threats " + std::to_string(e.threats));
    emit("info string eval initiative " + std::to_string(e.initiative));
    emit("info string eval total " + std::to_string(e.total));
  } else if (command == "features" || command == "nnueeval") {
#ifdef HEBICHESS_WASM
    emit("info string error WASM build supports HCE only");
#else
    if (command == "features") {
      for (Color perspective : {Color::White, Color::Black}) {
        const NnueFeatures features = extract_nnue_features(board_, perspective);
        std::ostringstream features_line;
        features_line << (perspective == Color::White ? "white" : "black");
        for (std::size_t i = 0; i < features.size; ++i) features_line << ' ' << features.indices[i];
        emit(features_line.str());
      }
    } else {
      const auto score = evaluate_nnue(board_);
      const auto raw_score = evaluate_nnue_network_raw(board_);
      if (score && raw_score) {
        std::ostringstream evaluation;
        evaluation << "nnue " << *score << " raw " << std::setprecision(9) << *raw_score;
        emit(evaluation.str());
      } else emit("info string error NNUE unavailable");
    }
#endif
  } else if (command == "go") {
    SearchLimits limits;
    limits.max_depth = 64;
    limits.eval_mode = eval_mode_;
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
        const int remaining = board_.side_to_move() == Color::White ? wtime : btime;
        const int increment = board_.side_to_move() == Color::White ? winc : binc;
        budget = remaining / 30 + increment / 2;
        budget = std::min(budget, std::max(1, remaining - 20));
      }
      budget = std::max(1, budget - (has_movetime ? std::min(20, budget / 10) : 10));
      limits.has_deadline = true;
      limits.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budget);
    }
    const SearchResult result = search(board_, limits, [this](int depth, int score, std::uint64_t nodes, std::uint64_t qnodes) {
      std::ostringstream info;
      if (score > MATE_SCORE - 1000 || score < -MATE_SCORE + 1000) {
        const int distance = std::max(1, (MATE_SCORE - std::abs(score) + 1) / 2);
        info << "info depth " << depth << " score mate " << (score >= 0 ? distance : -distance);
      } else info << "info depth " << depth << " score cp " << score;
      info << " nodes " << nodes << " qnodes " << qnodes;
      output_(info.str());
    });
    std::ostringstream stats;
    stats << "info string nodes " << result.nodes
          << " main_nodes " << result.main_nodes
          << " qnodes " << result.qnodes
          << " qdelta_prunes " << result.qdelta_prunes
          << " tt probes " << result.tt_probes
          << " hits " << result.tt_hits << " cutoffs " << result.tt_cutoffs
          << " see_calls " << result.see_calls
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
          << " aspiration_fail_lows " << result.aspiration_fail_lows;
    emit(stats.str());
    emit("bestmove " + move_to_uci(result.best_move));
  } else if (!command.empty() && command != "quit") {
    emit("info string error unsupported command");
  }
}

}  // namespace hebichess
