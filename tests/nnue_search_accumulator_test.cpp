#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "chess/nnue.hpp"
#include "chess/nnue_features.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"

using namespace hebichess;

namespace {

constexpr std::uint32_t kHidden1 = 32;
constexpr std::uint32_t kHidden2 = 32;
constexpr std::uint64_t kParameterCount =
    static_cast<std::uint64_t>(kNnueInputDimensions) * kNnueAccumulatorDimensions +
    kNnueAccumulatorDimensions + static_cast<std::uint64_t>(kHidden1) *
    2 * kNnueAccumulatorDimensions + kHidden1 +
    static_cast<std::uint64_t>(kHidden2) * kHidden1 + kHidden2 + kHidden2 + 1;

#pragma pack(push, 1)
struct HeaderV3 {
  char magic[8];
  std::uint32_t version, feature_abi, input_dimensions, accumulator_dimensions;
  std::uint32_t hidden1_dimensions, hidden2_dimensions, output_dimensions;
  std::uint32_t scalar_type, endian_marker, final_hidden_activation;
  float output_scale;
  std::uint64_t parameter_count, checksum;
};
#pragma pack(pop)

void require(bool condition, std::string_view message) {
  if (condition) return;
  std::cerr << "NNUE search accumulator test failure: " << message << '\n';
  std::abort();
}

std::uint64_t fnv1a_bytes(const std::uint8_t* bytes, std::size_t size) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 1099511628211ULL;
  }
  return hash;
}

float next_value(std::uint32_t& state, float limit) {
  state = state * 1664525U + 1013904223U;
  const float unit = static_cast<float>(state >> 8) / static_cast<float>(1U << 24);
  return (unit * 2.0F - 1.0F) * limit;
}

std::vector<std::uint8_t> deterministic_network() {
  std::vector<float> parameters;
  parameters.reserve(static_cast<std::size_t>(kParameterCount));
  std::uint32_t state = 0x6B1D2A9FU;
  const auto append = [&](std::size_t count, float limit) {
    for (std::size_t i = 0; i < count; ++i) parameters.push_back(next_value(state, limit));
  };
  append(kNnueInputDimensions * kNnueAccumulatorDimensions, 0.001F);
  for (std::size_t i = 0; i < kNnueAccumulatorDimensions; ++i)
    parameters.push_back(0.25F + next_value(state, 0.01F));
  append(static_cast<std::size_t>(kHidden1) * 2 * kNnueAccumulatorDimensions, 0.02F);
  for (std::size_t i = 0; i < kHidden1; ++i)
    parameters.push_back(0.05F + next_value(state, 0.01F));
  append(static_cast<std::size_t>(kHidden2) * kHidden1, 0.04F);
  for (std::size_t i = 0; i < kHidden2; ++i)
    parameters.push_back(0.20F + next_value(state, 0.01F));
  append(kHidden2, 100.0F);
  parameters.push_back(next_value(state, 10.0F));
  require(parameters.size() == kParameterCount, "deterministic parameter count");

  const auto* payload = reinterpret_cast<const std::uint8_t*>(parameters.data());
  const std::size_t payload_size = parameters.size() * sizeof(float);
  HeaderV3 header{{'H', 'E', 'B', 'I', 'N', 'N', 'U', 'E'}, 3, kNnueFeatureSetV1,
                  static_cast<std::uint32_t>(kNnueInputDimensions),
                  static_cast<std::uint32_t>(kNnueAccumulatorDimensions),
                  kHidden1, kHidden2, 1, 1, 0x01020304U, 2, 1.0F,
                  kParameterCount, fnv1a_bytes(payload, payload_size)};
  std::vector<std::uint8_t> bytes(sizeof(header) + payload_size);
  std::memcpy(bytes.data(), &header, sizeof(header));
  std::memcpy(bytes.data() + sizeof(header), payload, payload_size);
  return bytes;
}

struct Position {
  const char* name;
  const char* fen;
  int ctest_depth;
};

constexpr std::array<Position, 7> kPositions = {{
    {"startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4},
    {"quiet-middlegame", "rn1qk3/2p1p1b1/pp3npr/3p1p1p/P2P2P1/RPN4b/1BP1PP1P/1Q2KBNR w Kq - 1 12", 2},
    {"tactical-middlegame", "rn1q1k2/4p2r/ppp2npb/5p2/P2Pp3/BPN5/R1P1KPbP/1Q3BNR w - - 2 18", 2},
    {"king-attack", "2r1n1k1/3np2r/1ppq4/p1BNb1pQ/P1BPp3/1P6/2P2PbP/1R2K1NR w - - 14 30", 1},
    {"critical-phase4-index-9", "2r1n1k1/3nq1br/1pp1p3/p5p1/PN1Pp2P/1P1B4/2P2Pb1/2R1K1NR w - - 4 36", 2},
    {"critical-phase4-index-12", "1Nr1n1k1/8/1pp1p3/p1P4r/PP1Pp1pq/2R2bb1/4B3/2K3NR w - - 3 54", 1},
    {"critical-phase4-index-13", "1Nrq2k1/2n2r2/1pp1B3/p1P5/PP1Pp1p1/2R2b2/8/1K2b1NR w - - 5 60", 1},
}};

struct Options {
  std::vector<std::string_view> positions;
  std::optional<int> depth;
  bool all_baseline_positions{false};
  bool performance{false};
  int rounds{5};
};

void print_usage() {
  std::cout << "Usage: HebiChessNnueSearchAccumulatorTest [--position NAME]...\n"
               "       [--depth N --all-baseline-positions] [--performance] [--rounds N]\n"
               "Default runs the bounded CTest suite and targeted special-path checks.\n";
}

int parse_positive(const char* value, std::string_view flag) {
  try {
    const int parsed = std::stoi(value);
    if (parsed > 0) return parsed;
  } catch (const std::exception&) {
  }
  throw std::runtime_error(std::string(flag) + " requires a positive integer");
}

Options parse_options(int argc, char* argv[]) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view flag = argv[index];
    const auto next = [&]() -> const char* {
      if (++index >= argc) throw std::runtime_error(std::string(flag) + " needs a value");
      return argv[index];
    };
    if (flag == "--position") options.positions.emplace_back(next());
    else if (flag == "--depth") options.depth = parse_positive(next(), flag);
    else if (flag == "--all-baseline-positions") options.all_baseline_positions = true;
    else if (flag == "--performance") options.performance = true;
    else if (flag == "--rounds") options.rounds = parse_positive(next(), flag);
    else if (flag == "--help") {
      print_usage();
      std::exit(0);
    } else {
      throw std::runtime_error("unknown option: " + std::string(flag));
    }
  }
  if (options.all_baseline_positions && !options.depth)
    throw std::runtime_error("--all-baseline-positions requires --depth");
  if (options.depth && !options.all_baseline_positions && options.positions.empty())
    throw std::runtime_error("--depth requires --position or --all-baseline-positions");
  if (options.performance && (options.depth || options.all_baseline_positions || !options.positions.empty()))
    throw std::runtime_error("--performance cannot be combined with position or depth options");
  return options;
}

const Position& find_position(std::string_view name) {
  const auto found = std::find_if(kPositions.begin(), kPositions.end(),
                                  [&](const Position& position) { return position.name == name; });
  if (found == kPositions.end()) throw std::runtime_error("unknown position: " + std::string(name));
  return *found;
}

void require_same_search(const NnueSearchTestResult& legacy,
                         const NnueSearchTestResult& incremental,
                         std::string_view name) {
  const SearchResult& a = legacy.search;
  const SearchResult& b = incremental.search;
  require(a.best_move == b.best_move, std::string(name) + " bestmove");
  require(a.score == b.score, std::string(name) + " score");
  require(a.completed_depth == b.completed_depth, std::string(name) + " completed depth");
  require(a.nodes == b.nodes, std::string(name) + " nodes");
  require(a.qnodes == b.qnodes, std::string(name) + " qnodes");
}

void add_counters(NnueSearchAccumulatorCounters& total,
                  const NnueSearchAccumulatorCounters& value) {
  total.root_full_refresh_count += value.root_full_refresh_count;
  total.incremental_update_count += value.incremental_update_count;
  total.king_perspective_refresh_count += value.king_perspective_refresh_count;
  total.eval_from_accumulator_count += value.eval_from_accumulator_count;
  total.legacy_full_eval_count += value.legacy_full_eval_count;
  total.null_move_accumulator_reuse_count += value.null_move_accumulator_reuse_count;
  total.qsearch_incremental_update_count += value.qsearch_incremental_update_count;
  total.qsearch_eval_from_accumulator_count += value.qsearch_eval_from_accumulator_count;
  total.capture_incremental_update_count += value.capture_incremental_update_count;
  total.castling_incremental_update_count += value.castling_incremental_update_count;
  total.promotion_incremental_update_count += value.promotion_incremental_update_count;
  total.en_passant_incremental_update_count += value.en_passant_incremental_update_count;
}

void print_counters(const NnueSearchAccumulatorCounters& counters) {
  std::cout << " root_full_refresh_count=" << counters.root_full_refresh_count
            << " incremental_update_count=" << counters.incremental_update_count
            << " king_perspective_refresh_count=" << counters.king_perspective_refresh_count
            << " eval_from_accumulator_count=" << counters.eval_from_accumulator_count
            << " legacy_full_eval_count=" << counters.legacy_full_eval_count
            << " null_move_accumulator_reuse_count=" << counters.null_move_accumulator_reuse_count
            << " qsearch_incremental_update_count=" << counters.qsearch_incremental_update_count
            << " qsearch_eval_from_accumulator_count=" << counters.qsearch_eval_from_accumulator_count
            << " capture_incremental_update_count=" << counters.capture_incremental_update_count
            << " castling_incremental_update_count=" << counters.castling_incremental_update_count
            << " promotion_incremental_update_count=" << counters.promotion_incremental_update_count
            << " en_passant_incremental_update_count=" << counters.en_passant_incremental_update_count
            << '\n';
}

struct Comparison {
  NnueSearchTestResult legacy;
  NnueSearchTestResult incremental;
  double legacy_elapsed_ms{};
  double incremental_elapsed_ms{};
};

Comparison compare_search(const Board& board, const SearchLimits& limits,
                          std::string_view name) {
  clear_transposition_table();
  clear_search_heuristics();
  const auto legacy_started = std::chrono::steady_clock::now();
  NnueSearchTestResult legacy = search_nnue_legacy_for_test(board, limits);
  const double legacy_elapsed = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - legacy_started).count();

  clear_transposition_table();
  clear_search_heuristics();
  const auto incremental_started = std::chrono::steady_clock::now();
  NnueSearchTestResult incremental = search_nnue_incremental_for_test(board, limits);
  const double incremental_elapsed = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - incremental_started).count();
  require_same_search(legacy, incremental, name);
  require(incremental.counters.root_full_refresh_count == 1,
          std::string(name) + " root refresh count");
  require(incremental.counters.legacy_full_eval_count == 0,
          std::string(name) + " legacy full evaluation count");
  return {std::move(legacy), std::move(incremental), legacy_elapsed, incremental_elapsed};
}

void run_baseline_position(const Position& position, int depth,
                           NnueSearchAccumulatorCounters& totals) {
  const auto board = Board::from_fen(position.fen);
  require(board.has_value(), position.name);
  SearchLimits limits;
  limits.max_depth = depth;
  limits.eval_mode = EvalMode::NNUE;
  const Comparison comparison = compare_search(*board, limits, position.name);
  add_counters(totals, comparison.incremental.counters);
  const SearchResult& result = comparison.incremental.search;
  std::cout << "NNUE bounded equivalence position=" << position.name
            << " depth=" << depth
            << " bestmove=" << move_to_uci(result.best_move)
            << " score=" << result.score
            << " nodes=" << result.nodes
            << " qnodes=" << result.qnodes
            << " completed_depth=" << result.completed_depth;
  print_counters(comparison.incremental.counters);
}

struct SpecialPath {
  const char* name;
  const char* fen;
  int depth;
  std::uint64_t NnueSearchAccumulatorCounters::*counter;
};

constexpr std::array<SpecialPath, 6> kSpecialPaths = {{
    {"capture-heavy-qsearch", "3qk3/8/8/3r4/3Q4/8/8/3RK3 w - - 0 1", 1,
     &NnueSearchAccumulatorCounters::qsearch_incremental_update_count},
    {"king-move", "4k3/8/8/8/8/8/8/4K3 w - - 0 1", 1,
     &NnueSearchAccumulatorCounters::king_perspective_refresh_count},
    {"castling", "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", 1,
     &NnueSearchAccumulatorCounters::castling_incremental_update_count},
    {"promotion", "4k3/P7/8/8/8/8/8/4K3 w - - 0 1", 1,
     &NnueSearchAccumulatorCounters::promotion_incremental_update_count},
    {"en-passant", "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", 1,
     &NnueSearchAccumulatorCounters::en_passant_incremental_update_count},
    {"null-move", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4,
     &NnueSearchAccumulatorCounters::null_move_accumulator_reuse_count},
}};

void run_special_paths() {
  for (const SpecialPath& path : kSpecialPaths) {
    const auto board = Board::from_fen(path.fen);
    require(board.has_value(), path.name);
    SearchLimits limits;
    limits.max_depth = path.depth;
    limits.eval_mode = EvalMode::NNUE;
    const Comparison comparison = compare_search(*board, limits, path.name);
    const auto& counters = comparison.incremental.counters;
    require(counters.*(path.counter) > 0, std::string(path.name) + " accumulator path");
    if (std::string_view(path.name) == "capture-heavy-qsearch") {
      require(counters.qsearch_eval_from_accumulator_count > 0,
              "capture-heavy qsearch accumulator evaluation");
    }
    std::cout << "NNUE special-path=" << path.name << " depth=" << path.depth << " PASS";
    print_counters(counters);
  }
}

double median(std::vector<double> values) {
  require(!values.empty(), "median sample");
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

void run_performance(const Options& options) {
  struct PerformancePosition { const char* name; int depth; };
  constexpr std::array<PerformancePosition, 3> positions = {{
      // These retain useful fixed-depth work without the synthetic network's
      // capture-loop explosion seen in quiet-middlegame and index 12.
      {"startpos", 5}, {"tactical-middlegame", 3}, {"critical-phase4-index-9", 2},
  }};
  for (const PerformancePosition& entry : positions) {
    const Position& position = find_position(entry.name);
    const auto board = Board::from_fen(position.fen);
    require(board.has_value(), entry.name);
    SearchLimits limits;
    limits.max_depth = entry.depth;
    limits.eval_mode = EvalMode::NNUE;
    std::vector<double> legacy_nps;
    std::vector<double> incremental_nps;
    for (int round = 0; round < options.rounds; ++round) {
      const Comparison comparison = compare_search(*board, limits, entry.name);
      legacy_nps.push_back(static_cast<double>(comparison.legacy.search.nodes) * 1000.0 /
                           comparison.legacy_elapsed_ms);
      incremental_nps.push_back(static_cast<double>(comparison.incremental.search.nodes) * 1000.0 /
                                comparison.incremental_elapsed_ms);
    }
    const double legacy = median(legacy_nps);
    const double incremental = median(incremental_nps);
    std::cout << "NNUE server-perf position=" << entry.name << " depth=" << entry.depth
              << " rounds=" << options.rounds << " legacy_nps=" << legacy
              << " incremental_nps=" << incremental
              << " speedup_pct=" << (incremental / legacy - 1.0) * 100.0 << '\n';
  }
}

void run_bounded(const Options& options) {
  NnueSearchAccumulatorCounters totals;
  if (options.all_baseline_positions) {
    for (const Position& position : kPositions) run_baseline_position(position, *options.depth, totals);
  } else if (!options.positions.empty()) {
    for (std::string_view name : options.positions) {
      const Position& position = find_position(name);
      run_baseline_position(position, options.depth.value_or(position.ctest_depth), totals);
    }
  } else {
    for (const Position& position : kPositions) run_baseline_position(position, position.ctest_depth, totals);
  }
  require(totals.incremental_update_count > 0, "normal child updates");
  require(totals.eval_from_accumulator_count > 0, "accumulator evaluations");
  require(totals.qsearch_incremental_update_count > 0, "qsearch child updates");
  require(totals.qsearch_eval_from_accumulator_count > 0, "qsearch accumulator evaluations");
  // Shallow manual runs remain useful for diagnosis; default and depth >= 4
  // runs must cross null-move wiring.  The default special-path suite also
  // asserts that path independently.
  if (!options.depth || *options.depth >= 4)
    require(totals.null_move_accumulator_reuse_count > 0, "null move accumulator reuse");
  require(totals.legacy_full_eval_count == 0, "incremental mode legacy full evaluations");
  std::cout << "NNUE bounded totals positions="
            << (options.all_baseline_positions || options.positions.empty() ? kPositions.size() : options.positions.size());
  print_counters(totals);
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    const Options options = parse_options(argc, argv);
    const std::vector<std::uint8_t> network = deterministic_network();
    std::string error;
    require(load_nnue_network_bytes(network.data(), network.size(), error), "load deterministic network");
    if (options.performance) {
      run_performance(options);
    } else {
      run_bounded(options);
      if (!options.depth && options.positions.empty()) run_special_paths();
    }
  } catch (const std::exception& error) {
    std::cerr << "NNUE search accumulator test error: " << error.what() << '\n';
    return 2;
  }
}
