#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "chess/nnue.hpp"
#include "chess/search.hpp"

using namespace hebichess;

namespace {

struct Position { std::string name; Board board; };
struct Aggregate {
  std::uint64_t nodes{};
  std::uint64_t qnodes{};
  std::uint64_t eval_calls{};
  std::uint64_t qdelta_prunes{};
  std::uint64_t qtt_probes{};
  std::uint64_t qtt_hits{};
  std::uint64_t qtt_cutoffs{};
  std::uint64_t qtt_stores{};
  std::uint64_t qtt_replacements{};
  std::uint64_t tt_replacements{};
};

std::vector<Position> load_positions(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open fixture: " + path);
  std::vector<Position> positions;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') continue;
    const std::size_t tab = line.find('\t');
    const auto board = tab == std::string::npos ? std::nullopt : Board::from_fen(line.substr(tab + 1));
    if (!board) throw std::runtime_error("invalid fixture line");
    positions.push_back({line.substr(0, tab), *board});
  }
  return positions;
}

void add(Aggregate& total, const SearchResult& result) {
  total.nodes += result.nodes;
  total.qnodes += result.qnodes;
  total.eval_calls += result.eval_calls;
  total.qdelta_prunes += result.qdelta_prunes;
  total.qtt_probes += result.qtt_probes;
  total.qtt_hits += result.qtt_hits;
  total.qtt_cutoffs += result.qtt_active_cutoffs;
  total.qtt_stores += result.qtt_stores;
  total.qtt_replacements += result.qtt_replacements;
  total.tt_replacements += result.tt_replacements;
}

struct Sample { double elapsed_ms{}; Aggregate aggregate{}; };

Sample run_once(const std::vector<Position>& positions, int depth) {
  Sample sample;
  const auto started = std::chrono::steady_clock::now();
  for (const Position& position : positions) {
    clear_transposition_table();
    clear_search_heuristics();
    SearchLimits limits;
    limits.max_depth = depth;
    limits.eval_mode = EvalMode::NNUE;
    const SearchResult result = search(position.board, limits);
    add(sample.aggregate, result);
  }
  sample.elapsed_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - started).count();
  return sample;
}

void print(const char* label, const Sample& sample) {
  const Aggregate& a = sample.aggregate;
  const double nps = a.nodes * 1000.0 / std::max(0.001, sample.elapsed_ms);
  const double qshare = a.nodes == 0 ? 0.0 : 100.0 * a.qnodes / a.nodes;
  const double hit_rate = a.qtt_probes == 0 ? 0.0 : 100.0 * a.qtt_hits / a.qtt_probes;
  std::cout << std::fixed << std::setprecision(3)
            << label << " elapsed_ms=" << sample.elapsed_ms << " nps=" << nps
            << " nodes=" << a.nodes << " qnodes=" << a.qnodes << " qshare_pct=" << qshare
            << " eval_calls=" << a.eval_calls << " qtt_probes=" << a.qtt_probes
            << " qdelta_prunes=" << a.qdelta_prunes
            << " qtt_hits=" << a.qtt_hits << " qtt_hit_rate_pct=" << hit_rate
            << " qtt_cutoffs=" << a.qtt_cutoffs << " qtt_stores=" << a.qtt_stores
            << " qtt_replacements=" << a.qtt_replacements
            << " main_tt_replacements=" << a.tt_replacements << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    std::string fixture = "tests/data/phase6-search-baseline.fen";
    std::string network;
    std::string only;
    int depth = 5;
    int rounds = 5;
    for (int index = 1; index < argc; ++index) {
      const std::string flag = argv[index];
      if (++index >= argc) throw std::runtime_error(flag + " needs a value");
      const std::string value = argv[index];
      if (flag == "--fixture") fixture = value;
      else if (flag == "--network") network = value;
      else if (flag == "--only") only = value;
      else if (flag == "--depth") depth = std::stoi(value);
      else if (flag == "--rounds") rounds = std::stoi(value);
      else throw std::runtime_error("unknown option: " + flag);
    }
    if (network.empty()) throw std::runtime_error("--network is required");
    std::string network_error;
    if (!load_nnue_network(network, network_error)) throw std::runtime_error(network_error);
    std::vector<Position> positions = load_positions(fixture);
    if (!only.empty()) {
      positions.erase(std::remove_if(positions.begin(), positions.end(), [&](const Position& position) {
        return position.name != only;
      }), positions.end());
      if (positions.empty()) throw std::runtime_error("fixture has no position named: " + only);
    }
    // Warmup never contributes to a reported median.
    (void)run_once(positions, depth);
    std::vector<Sample> samples;
    for (int round = 0; round < rounds; ++round) samples.push_back(run_once(positions, depth));
    std::sort(samples.begin(), samples.end(), [](const Sample& a, const Sample& b) {
      return a.elapsed_ms < b.elapsed_ms;
    });
    print("median", samples[samples.size() / 2]);
  } catch (const std::exception& error) {
    std::cerr << "qsearch-tt-benchmark: " << error.what() << '\n';
    return 1;
  }
}
