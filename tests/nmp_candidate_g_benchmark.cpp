#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

#include "chess/nnue.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"

using namespace hebichess;

namespace {

struct Case {
  const char* name;
  const char* fen;
};

constexpr const char* kRootFen =
    "8/2b2p2/2p3p1/p1k4p/K7/1B4P1/7P/8 b - - 0 1";

const std::vector<Case>& corpus() {
  static const std::vector<Case> cases = {
      {"root", kRootFen},
      {"false_cutoff",
       "8/2b5/2p3B1/p6p/K7/2k3P1/7P/8 b - - 0 3"},
      {"same_game_before",
       "8/2b2p2/2p3p1/p1k5/K6P/1B6/7P/8 b - - 0 2"},
      {"same_game_after",
       "8/2b2p2/2p3p1/p1k4p/K7/1B4P1/7P/8 b - - 0 1"},
      {"bishop_same_color", "8/8/2b5/8/4k3/8/2B5/4K3 w - - 0 1"},
      {"bishop_opposite_color", "8/8/1b6/8/4k3/8/2B5/4K3 w - - 0 1"},
      {"knight_pawn", "8/8/8/3n4/3P4/8/8/4K1k1 w - - 0 1"},
      {"minor_vs_pawns", "8/8/2b5/8/3P4/4P3/8/4K1k1 w - - 0 1"},
      {"passed_pawns", "8/5p2/8/8/8/2P5/8/4K1k1 w - - 0 1"},
      {"startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
      {"middlegame",
       "rn1qk3/2p1p1b1/pp3npr/3p1p1p/P2P2P1/RPN4b/1BP1PP1P/1Q2KBNR w Kq - 1 12"},
  };
  return cases;
}

std::string root_candidates(const SearchResult& result) {
  std::string value;
  for (const RootMoveInfo& move : result.root_moves) {
    if (!value.empty()) value += ';';
    value += move_to_uci(move.move) + ':' + std::to_string(move.search_score) + ':';
    value += move.bound == ScoreBound::Exact ? "Exact" :
             move.bound == ScoreBound::Lower ? "Lower" : "Upper";
  }
  return value;
}

void run_case(const Case& item, int depth) {
  const auto board = Board::from_fen(item.fen);
  if (!board) throw std::runtime_error(std::string("invalid FEN: ") + item.name);
  clear_transposition_table();
  clear_search_heuristics();
  SearchLimits limits;
  limits.max_depth = depth;
  limits.eval_mode = EvalMode::NNUE;
  limits.use_root_style_selection = false;
  const auto started = std::chrono::steady_clock::now();
  const SearchResult result = search(*board, limits);
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started).count();
  const double seconds = std::max(0.001, elapsed_ms / 1000.0);
#if HEBICHESS_NMP_CANDIDATE_G
  const auto g_attempts = result.nmp_g_verification_attempts;
  const auto g_confirmed = result.nmp_g_confirmed_verifications;
  const auto g_rejected = result.nmp_g_rejected_verifications;
  const auto g_nodes = result.nmp_g_verification_nodes;
  const auto g_qnodes = result.nmp_g_verification_qnodes;
#else
  const std::uint64_t g_attempts = 0;
  const std::uint64_t g_confirmed = 0;
  const std::uint64_t g_rejected = 0;
  const std::uint64_t g_nodes = 0;
  const std::uint64_t g_qnodes = 0;
#endif
  std::cout << item.name << ',' << depth << ',' << move_to_uci(result.best_move) << ','
            << result.score << ',' << result.nodes << ',' << result.qnodes << ','
            << std::fixed << std::setprecision(2) << result.nodes / seconds << ','
            << elapsed_ms << ',' << result.null_attempts << ',' << result.null_cutoffs << ','
             << g_attempts << ',' << g_confirmed << ',' << g_rejected << ','
             << g_nodes << ',' << g_qnodes << ',' << result.aspiration_retries << ',' << '"'
             << root_candidates(result) << '"' << "\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    if (argc < 3 || std::string(argv[1]) != "--network")
      throw std::runtime_error("usage: HebiChessNmpCandidateGBenchmark --network model.hebinnue [--root-only] [--depths 4,6]");
    bool root_only = false;
    std::vector<int> depths{4, 6, 8, 10, 12};
    for (int index = 3; index < argc; ++index) {
      const std::string option = argv[index];
      if (option == "--root-only") root_only = true;
      else if (option == "--depths" && index + 1 < argc) {
        depths.clear();
        std::stringstream stream(argv[++index]);
        std::string token;
        while (std::getline(stream, token, ',')) depths.push_back(std::stoi(token));
      } else throw std::runtime_error("unknown benchmark option: " + option);
    }
    std::string error;
    if (!load_nnue_network(argv[2], error)) throw std::runtime_error(error);
    std::cout << "name,depth,bestmove,score,nodes,qnodes,nps,elapsed_ms,null_attempts,null_cutoffs,"
                 "g_attempts,g_confirmed,g_rejected,g_nodes,g_qnodes,aspiration_retries,root_candidates\n";
    for (const Case& item : corpus()) {
      if (root_only && std::string(item.name) != "root") continue;
      for (int depth : depths) run_case(item, depth);
    }
  } catch (const std::exception& error) {
    std::cerr << "NMP candidate benchmark failure: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
