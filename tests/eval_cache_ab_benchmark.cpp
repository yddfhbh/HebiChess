#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "chess/nnue.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"

using namespace hebichess;

namespace {
struct Fixture { std::string name; std::string fen; int depth; };
const Fixture known_fixtures[] = {
    {"endgame", "8/2b2p2/2p3p1/p1k4p/K7/1B4P1/7P/8 b - - 0 1", 10},
    {"startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5},
    {"quiet", "rn1qk3/2p1p1b1/pp3npr/3p1p1p/P2P2P1/RPN4b/1BP1PP1P/1Q2KBNR w Kq - 1 12", 5},
    {"tactical", "rn1q1k2/4p2r/ppp2npb/5p2/P2Pp3/BPN5/R1P1KPbP/1Q3BNR w - - 2 18", 5},
    {"king_attack", "2r1n1k1/3np2r/1ppq4/p1BNb1pQ/P1BPp3/1P6/2P2PbP/1R2K1NR w - - 14 30", 5},
};

std::vector<Fixture> load_fixtures(const char* corpus_path) {
  std::vector<Fixture> fixtures(std::begin(known_fixtures), std::end(known_fixtures));
  std::ifstream input(corpus_path);
  if (!input) throw std::runtime_error(std::string("cannot open corpus: ") + corpus_path);
  std::string line;
  std::size_t line_number = 0;
  std::size_t selected = 0;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') continue;
    if (line_number++ % 6 != 0) continue;
    const auto tab = line.find('\t');
    const std::string fen = tab == std::string::npos ? line : line.substr(tab + 1);
    fixtures.push_back({"corpus-" + std::to_string(selected++), fen, 4});
    if (selected == 15) break;
  }
  if (selected < 15) throw std::runtime_error("deterministic corpus has fewer than 15 sampled positions");
  return fixtures;
}

std::string pv_text(const Board& board, const SearchResult& result) {
  std::string text;
  for (const Move& move : result.principal_variation) {
    if (!text.empty()) text += ',';
    text += move_to_uci(move);
  }
  return text;
}
}

int main(int argc, char** argv) {
  try {
    if (argc < 3 || argc > 4)
      throw std::runtime_error("usage: HebiChessEvalCacheAB NETWORK CORPUS [ROUNDS]");
    const int rounds = argc == 4 ? std::stoi(argv[3]) : 4;
    if (rounds < 1 || rounds > 16) throw std::runtime_error("ROUNDS must be 1..16");
    std::string error;
    if (!load_nnue_network(argv[1], error)) throw std::runtime_error(error);
    const std::vector<Fixture> fixtures = load_fixtures(argv[2]);
    for (const Fixture& fixture : fixtures) {
      const auto board = Board::from_fen(fixture.fen);
      if (!board) throw std::runtime_error(std::string("bad fixture: ") + fixture.name);
      SearchLimits limits;
      limits.max_depth = fixture.depth;
      limits.eval_mode = EvalMode::NNUE;
      std::vector<double> timings;
      SearchResult reference;
      clear_transposition_table();
      clear_search_heuristics();
      (void)search(*board, limits);  // Untimed warm-up for this position/configuration.
      for (int rep = 0; rep < rounds; ++rep) {
        clear_transposition_table();
        clear_search_heuristics();
        const auto begin = std::chrono::steady_clock::now();
        SearchResult result = search(*board, limits);
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - begin).count();
        if (rep == 0) reference = std::move(result);
        else if (result.best_move != reference.best_move || result.score != reference.score ||
                 result.completed_depth != reference.completed_depth ||
                 result.nodes != reference.nodes || result.qnodes != reference.qnodes ||
                 result.aspiration_retries != reference.aspiration_retries ||
                 pv_text(*board, result) != pv_text(*board, reference))
          throw std::runtime_error(std::string("nondeterministic run: ") + fixture.name);
        timings.push_back(ms);
      }
      std::sort(timings.begin(), timings.end());
      std::cout << std::fixed << std::setprecision(3) << fixture.name
                << " depth=" << fixture.depth
                << " median_ms=" << timings[timings.size() / 2]
                << " completed_depth=" << reference.completed_depth
                << " bestmove=" << move_to_uci(reference.best_move)
                << " score=" << reference.score << " nodes=" << reference.nodes
                << " qnodes=" << reference.qnodes
                << " retries=" << reference.aspiration_retries
                << " pv=" << pv_text(*board, reference);
#if defined(HEBICHESS_EVALCACHE_EXACT)
      std::cout << " cache_lookups=" << reference.eval_cache_lookups
                << " cache_hits=" << reference.eval_cache_hits
                << " cache_misses=" << reference.eval_cache_misses
                << " cache_collisions=" << reference.eval_cache_key_collisions
                << " same_key_acc_mismatch=" << reference.eval_cache_accumulator_mismatches
                << " inserts=" << reference.eval_cache_inserts
                << " replacements=" << reference.eval_cache_replacements
                << " key_comparisons=" << reference.eval_cache_board_key_comparisons
                << " key_matches=" << reference.eval_cache_board_key_matches
                << " memcmp_calls=" << reference.eval_cache_memcmp_calls
                << " memcmp_bytes=" << reference.eval_cache_memcmp_bytes
                << " ways_examined=" << reference.eval_cache_ways_examined
                << " entry_bytes=" << reference.eval_cache_entry_bytes
                << " accumulator_bytes=" << reference.eval_cache_accumulator_bytes
                << " storage_bytes=" << reference.eval_cache_storage_bytes
                << " clear_ns=" << reference.eval_cache_clear_ns
                << " capacity=" << reference.eval_cache_capacity
                << " ways=" << reference.eval_cache_ways
                << " hit_lookup_sample_ns="
                << (reference.eval_cache_timed_hit_samples == 0 ? 0 :
                    reference.eval_cache_hit_lookup_ns / reference.eval_cache_timed_hit_samples)
                << " miss_lookup_sample_ns="
                << (reference.eval_cache_timed_miss_samples == 0 ? 0 :
                    reference.eval_cache_miss_lookup_ns / reference.eval_cache_timed_miss_samples)
                << " insertion_sample_ns="
                << (reference.eval_cache_timed_insert_samples == 0 ? 0 :
                    reference.eval_cache_insert_ns / reference.eval_cache_timed_insert_samples)
                << " replacement_sample_ns="
                << (reference.eval_cache_timed_replacement_samples == 0 ? 0 :
                    reference.eval_cache_replacement_ns / reference.eval_cache_timed_replacement_samples);
#endif
      std::cout << '\n';
    }
  } catch (const std::exception& e) {
    std::cerr << "eval-cache-ab: " << e.what() << '\n';
    return 1;
  }
}
