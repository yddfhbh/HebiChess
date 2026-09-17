// Test-only fixed-layout NNUE search acceptance harness.  Each CMake target
// compiles this exact source with one immutable hidden1 layout definition.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "chess/nnue.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"

using namespace hebichess;

namespace {

#if HEBICHESS_NNUE_HIDDEN1_LAYOUT == 0
constexpr std::string_view kLayout = "Original4";
#elif HEBICHESS_NNUE_HIDDEN1_LAYOUT == 4
constexpr std::string_view kLayout = "Prepacked4";
#elif HEBICHESS_NNUE_HIDDEN1_LAYOUT == 8
constexpr std::string_view kLayout = "Prepacked8";
#else
#error "unexpected test-only hidden1 layout"
#endif

struct Options {
  std::filesystem::path fixture{"tests/data/nnue-search-benchmark.fen"};
  std::filesystem::path output{"runs/nnue-search-layout.json"};
  std::filesystem::path summary{"runs/nnue-search-layout.summary.txt"};
  std::filesystem::path network;
  int depth{5};
  int time_ms{0};
  int rounds{1};
};

struct Position {
  std::string fen;
  Board board;
};

struct Record {
  int round{};
  std::size_t position{};
  SearchResult result{};
  double elapsed_ms{};
  NnueVariantParity parity{};
};

std::string require_value(int& index, int argc, char* argv[], const char* flag) {
  if (++index >= argc) throw std::runtime_error(std::string(flag) + " needs a value");
  return argv[index];
}

int positive_int(const std::string& value, const char* flag, bool allow_zero = false) {
  try {
    const int parsed = std::stoi(value);
    if (parsed < 0 || (!allow_zero && parsed == 0)) throw std::runtime_error("range");
    return parsed;
  } catch (const std::exception&) {
    throw std::runtime_error(std::string(flag) + (allow_zero ? " needs a non-negative integer"
                                                             : " needs a positive integer"));
  }
}

Options parse_options(int argc, char* argv[]) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string flag = argv[index];
    if (flag == "--network") options.network = require_value(index, argc, argv, "--network");
    else if (flag == "--output") options.output = require_value(index, argc, argv, "--output");
    else if (flag == "--summary") options.summary = require_value(index, argc, argv, "--summary");
    else if (flag == "--fixture") options.fixture = require_value(index, argc, argv, "--fixture");
    else if (flag == "--depth") options.depth = positive_int(require_value(index, argc, argv, "--depth"), "--depth");
    else if (flag == "--time-ms") options.time_ms = positive_int(require_value(index, argc, argv, "--time-ms"), "--time-ms", true);
    else if (flag == "--rounds") options.rounds = positive_int(require_value(index, argc, argv, "--rounds"), "--rounds");
    else if (flag == "--help") {
      std::cout << "Usage: search target --network MODEL [--depth N] [--time-ms N] [--output JSON] [--summary TEXT] [--fixture FEN] [--rounds N]\n";
      std::exit(0);
    } else throw std::runtime_error("unknown option: " + flag);
  }
  if (options.network.empty()) throw std::runtime_error("--network is required");
  return options;
}

std::vector<Position> load_positions(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open fixture: " + path.string());
  std::vector<Position> result;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') continue;
    const auto board = Board::from_fen(line);
    if (!board) throw std::runtime_error("invalid FEN: " + line);
    result.push_back({line, *board});
  }
  if (result.empty()) throw std::runtime_error("fixture has no positions");
  return result;
}

double elapsed_ms(std::chrono::steady_clock::time_point started) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}

void make_parent(const std::filesystem::path& path) {
  if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
}

std::string json_escape(std::string_view value) {
  std::string escaped;
  for (char ch : value) {
    if (ch == '"' || ch == '\\') escaped.push_back('\\');
    escaped.push_back(ch);
  }
  return escaped;
}

Record run_one(const Position& position, const Options& options, int round, std::size_t index) {
  clear_transposition_table();
  clear_search_heuristics();
  SearchLimits limits;
  limits.max_depth = options.depth;
  limits.eval_mode = EvalMode::NNUE;
  if (options.time_ms != 0) {
    limits.has_deadline = true;
    limits.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options.time_ms);
  }
  const auto started = std::chrono::steady_clock::now();
  const SearchResult result = search(position.board, limits);
  const auto parity = nnue_variant_parity_for_test(position.board);
  if (!parity) throw std::runtime_error("NNUE parity probe failed");
  return {round, index, result, elapsed_ms(started), *parity};
}

void write_json(const Options& options, const std::vector<Position>& positions,
                const std::vector<Record>& records) {
  make_parent(options.output);
  std::ofstream out(options.output);
  if (!out) throw std::runtime_error("cannot write output: " + options.output.string());
  out << std::fixed << std::setprecision(6);
  out << "{\n  \"schema\": \"hebichess-nnue-layout-search-v1\",\n"
      << "  \"layout\": \"" << kLayout << "\",\n"
      << "  \"depth\": " << options.depth << ",\n"
      << "  \"time_ms\": " << options.time_ms << ",\n"
      << "  \"positions\": " << positions.size() << ",\n"
      << "  \"records\": [\n";
  for (std::size_t i = 0; i < records.size(); ++i) {
    const Record& r = records[i];
    if (i) out << ",\n";
    out << "    {\"round\":" << r.round << ",\"position\":" << r.position
        << ",\"fen\":\"" << json_escape(positions[r.position].fen) << "\""
        << ",\"bestmove\":\"" << move_to_uci(r.result.best_move) << "\""
        << ",\"score_cp\":" << r.result.score
        << ",\"completed_depth\":" << r.result.completed_depth
        << ",\"nodes\":" << r.result.nodes << ",\"qnodes\":" << r.result.qnodes
        << ",\"elapsed_ms\":" << r.elapsed_ms
        << ",\"nps\":" << r.result.nodes * 1000.0 / std::max(r.elapsed_ms, 0.001)
        << ",\"hidden1_max_abs_diff\":" << r.parity.hidden1_max_abs_diff
        << ",\"raw_max_abs_diff\":" << r.parity.raw_max_abs_diff
        << ",\"active_cp\":" << r.parity.active_cp
        << ",\"original4_cp\":" << r.parity.original4_cp << '}';
  }
  out << "\n  ]\n}\n";
}

void write_summary(const Options& options, const std::vector<Record>& records) {
  make_parent(options.summary);
  std::ofstream out(options.summary);
  if (!out) throw std::runtime_error("cannot write summary: " + options.summary.string());
  double total_elapsed = 0.0;
  std::uint64_t total_nodes = 0;
  float max_hidden1_diff = 0.0F, max_raw_diff = 0.0F;
  std::uint64_t cp_mismatches = 0;
  for (const Record& r : records) {
    total_elapsed += r.elapsed_ms;
    total_nodes += r.result.nodes;
    max_hidden1_diff = std::max(max_hidden1_diff, r.parity.hidden1_max_abs_diff);
    max_raw_diff = std::max(max_raw_diff, r.parity.raw_max_abs_diff);
    cp_mismatches += r.parity.active_cp != r.parity.original4_cp;
  }
  out << std::fixed << std::setprecision(3)
      << "layout=" << kLayout << " depth=" << options.depth << " time_ms=" << options.time_ms
      << " records=" << records.size() << " total_nodes=" << total_nodes
      << " aggregate_nps=" << total_nodes * 1000.0 / std::max(total_elapsed, 0.001)
      << " hidden1_max_abs_diff=" << max_hidden1_diff
      << " raw_max_abs_diff=" << max_raw_diff << " cp_mismatches=" << cp_mismatches << '\n';
  for (const Record& r : records) {
    out << "round=" << r.round << " position=" << r.position
        << " bestmove=" << move_to_uci(r.result.best_move) << " score=" << r.result.score
        << " nodes=" << r.result.nodes << " qnodes=" << r.result.qnodes
        << " nps=" << r.result.nodes * 1000.0 / std::max(r.elapsed_ms, 0.001) << '\n';
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    const Options options = parse_options(argc, argv);
    std::string error;
    if (!load_nnue_network(options.network.string(), error)) throw std::runtime_error(error);
    const std::vector<Position> positions = load_positions(options.fixture);
    std::vector<Record> records;
    records.reserve(static_cast<std::size_t>(options.rounds) * positions.size());
    for (int round = 0; round < options.rounds; ++round)
      for (std::size_t index = 0; index < positions.size(); ++index)
        records.push_back(run_one(positions[index], options, round + 1, index));
    write_json(options, positions, records);
    write_summary(options, records);
    std::cout << kLayout << " wrote " << options.output << " and " << options.summary << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << kLayout << " search layout benchmark failed: " << error.what() << '\n';
    return 2;
  }
}
