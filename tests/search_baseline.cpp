#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "chess/eval.hpp"
#include "chess/nnue.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"

using namespace hebichess;

namespace {

struct Position { std::string name; std::string fen; Board board; };
struct Options {
  std::filesystem::path fixture{"tests/data/phase6-search-baseline.fen"};
  std::filesystem::path output{"runs/phase6-search-baseline.json"};
  std::filesystem::path summary{};
  std::optional<std::filesystem::path> network;
  int depth{5};
  std::vector<int> time_ms{1000, 3000};
  std::vector<EvalMode> modes{EvalMode::HCE, EvalMode::NNUE};
  int eval_warmup{1000};
  int eval_iterations{10000};
  int eval_samples{5};
};

std::string mode_name(EvalMode mode) { return mode == EvalMode::HCE ? "HCE" : "NNUE"; }

std::string json_escape(std::string_view value) {
  std::string escaped;
  for (char ch : value) {
    if (ch == '"' || ch == '\\') escaped.push_back('\\');
    escaped.push_back(ch);
  }
  return escaped;
}

std::vector<int> parse_positive_csv(const std::string& value, const char* flag) {
  std::vector<int> result;
  std::stringstream stream(value);
  std::string item;
  while (std::getline(stream, item, ',')) {
    try {
      const int parsed = std::stoi(item);
      if (parsed <= 0) throw std::invalid_argument("non-positive");
      result.push_back(parsed);
    } catch (const std::exception&) {
      throw std::runtime_error(std::string(flag) + " needs positive comma-separated integers");
    }
  }
  if (result.empty()) throw std::runtime_error(std::string(flag) + " must not be empty");
  return result;
}

std::vector<EvalMode> parse_modes(const std::string& value) {
  std::vector<EvalMode> result;
  std::stringstream stream(value);
  std::string item;
  while (std::getline(stream, item, ',')) {
    if (item == "hce" || item == "HCE") result.push_back(EvalMode::HCE);
    else if (item == "nnue" || item == "NNUE") result.push_back(EvalMode::NNUE);
    else throw std::runtime_error("--modes accepts hce and nnue");
  }
  if (result.empty()) throw std::runtime_error("--modes must not be empty");
  if (std::count(result.begin(), result.end(), EvalMode::HCE) > 1 ||
      std::count(result.begin(), result.end(), EvalMode::NNUE) > 1)
    throw std::runtime_error("--modes must not repeat a mode");
  return result;
}

int parse_positive(const std::string& value, const char* flag) {
  const auto parsed = parse_positive_csv(value, flag);
  if (parsed.size() != 1) throw std::runtime_error(std::string(flag) + " accepts one integer");
  return parsed.front();
}

Options parse_options(int argc, char* argv[]) {
  Options options;
  auto next = [&](int& index, const char* flag) -> std::string {
    if (++index >= argc) throw std::runtime_error(std::string(flag) + " needs a value");
    return argv[index];
  };
  for (int index = 1; index < argc; ++index) {
    const std::string flag = argv[index];
    if (flag == "--fixture") options.fixture = next(index, "--fixture");
    else if (flag == "--output") options.output = next(index, "--output");
    else if (flag == "--summary") options.summary = next(index, "--summary");
    else if (flag == "--network") options.network = next(index, "--network");
    else if (flag == "--depth") options.depth = parse_positive(next(index, "--depth"), "--depth");
    else if (flag == "--time-ms") options.time_ms = parse_positive_csv(next(index, "--time-ms"), "--time-ms");
    else if (flag == "--modes") options.modes = parse_modes(next(index, "--modes"));
    else if (flag == "--eval-warmup") options.eval_warmup = parse_positive(next(index, "--eval-warmup"), "--eval-warmup");
    else if (flag == "--eval-iters") options.eval_iterations = parse_positive(next(index, "--eval-iters"), "--eval-iters");
    else if (flag == "--eval-samples") options.eval_samples = parse_positive(next(index, "--eval-samples"), "--eval-samples");
    else if (flag == "--help") {
      std::cout << "Usage: HebiChessSearchBaseline [--fixture path] [--output json] [--summary text]"
                   " [--network model.hebinnue] [--depth N] [--time-ms N[,N...]]"
                   " [--modes hce,nnue] [--eval-warmup N] [--eval-iters N] [--eval-samples N]\\n";
      std::exit(0);
    } else throw std::runtime_error("unknown option: " + flag);
  }
  if (options.summary.empty()) {
    options.summary = options.output;
    options.summary.replace_extension(".summary.txt");
  }
  return options;
}

std::vector<Position> load_positions(const std::filesystem::path& fixture) {
  std::ifstream input(fixture);
  if (!input) throw std::runtime_error("cannot open fixture: " + fixture.string());
  std::vector<Position> positions;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty() || line[0] == '#') continue;
    const auto tab = line.find('\t');
    if (tab == std::string::npos || tab == 0 || tab + 1 == line.size())
      throw std::runtime_error("fixture line " + std::to_string(line_number) + " needs name<TAB>FEN");
    const std::string name = line.substr(0, tab);
    const std::string fen = line.substr(tab + 1);
    const auto board = Board::from_fen(fen);
    if (!board) throw std::runtime_error("invalid FEN at fixture line " + std::to_string(line_number));
    positions.push_back({name, fen, *board});
  }
  if (positions.empty()) throw std::runtime_error("fixture contains no positions");
  return positions;
}

double milliseconds_since(std::chrono::steady_clock::time_point started) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}
double ratio(std::uint64_t numerator, std::uint64_t denominator) {
  return denominator == 0 ? 0.0 : static_cast<double>(numerator) / static_cast<double>(denominator);
}

void ensure_parent_directory(const std::filesystem::path& path) {
  if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
}

void write_search_fields(std::ostream& out, const SearchResult& r) {
  out << "\"bestmove\":\"" << json_escape(move_to_uci(r.best_move)) << "\","
      << "\"score_cp\":" << r.score << ",\"completed_depth\":" << r.completed_depth
      << ",\"nodes\":" << r.nodes << ",\"main_nodes\":" << r.main_nodes
      << ",\"qnodes\":" << r.qnodes << ",\"qnodes_total_ratio\":" << ratio(r.qnodes, r.nodes)
      << ",\"q_stand_pat_beta_cutoffs\":" << r.q_stand_pat_beta_cutoffs
      << ",\"q_in_check_nodes\":" << r.q_in_check_nodes
      << ",\"q_non_check_nodes\":" << r.q_non_check_nodes
      << ",\"q_tactical_generated\":" << r.q_tactical_generated
      << ",\"q_tactical_searched\":" << r.q_tactical_searched
      << ",\"q_see_pruned\":" << r.q_see_pruned
      << ",\"q_delta_pruned\":" << r.q_delta_pruned
      << ",\"q_stalemate_full_movegen_calls\":" << r.q_stalemate_full_movegen_calls
      << ",\"q_max_ply\":" << r.q_max_ply
      << ",\"q_check_evasion_generated\":" << r.q_check_evasion_generated
      << ",\"q_check_evasion_searched\":" << r.q_check_evasion_searched
      << ",\"q_nnue_evals\":" << r.q_nnue_evals
      << ",\"q_nnue_incremental_updates\":" << r.q_nnue_incremental_updates
      << ",\"q_see_calls\":" << r.q_see_calls
      << ",\"q_gives_check_calls\":" << r.q_gives_check_calls
      << ",\"q_gives_check_skipped\":" << r.q_gives_check_skipped
      << ",\"q_gives_check_for_see_exception\":" << r.q_gives_check_for_see_exception
      << ",\"q_gives_check_for_delta_exception\":" << r.q_gives_check_for_delta_exception
      << ",\"q_gives_check_for_ordering\":" << r.q_gives_check_for_ordering
      << ",\"q_evasion_check_ordering_calls\":" << r.q_evasion_check_ordering_calls
      << ",\"q_full_legal_movegen_calls\":" << r.q_full_legal_movegen_calls
      << ",\"q_tactical_movegen_calls\":" << r.q_tactical_movegen_calls
      << ",\"qtt_probes\":" << r.qtt_probes << ",\"qtt_hits\":" << r.qtt_hits
      << ",\"qtt_cutoffs\":" << r.qtt_cutoffs
      << ",\"qdelta_prunes\":" << r.qdelta_prunes
      << ",\"tt_probes\":" << r.tt_probes << ",\"tt_hits\":" << r.tt_hits
      << ",\"tt_hit_rate\":" << ratio(r.tt_hits, r.tt_probes)
      << ",\"tt_cutoffs\":" << r.tt_cutoffs << ",\"tt_cutoff_rate\":" << ratio(r.tt_cutoffs, r.tt_probes)
      << ",\"see_calls\":" << r.see_calls << ",\"see_prunes\":" << r.see_prunes
      << ",\"killer_cutoffs\":" << r.killer_cutoffs << ",\"killer_uses\":" << r.killer_uses
      << ",\"history_cutoffs\":" << r.history_cutoffs
      << ",\"null_attempts\":" << r.null_attempts << ",\"null_cutoffs\":" << r.null_cutoffs
      << ",\"null_cutoff_rate\":" << ratio(r.null_cutoffs, r.null_attempts)
      << ",\"lmr_attempts\":" << r.lmr_attempts << ",\"lmr_researches\":" << r.lmr_researches
      << ",\"lmr_reduced_search_nodes\":" << r.lmr_reduced_search_nodes
      << ",\"lmr_research_nodes\":" << r.lmr_research_nodes
      << ",\"pvs_zero_window_searches\":" << r.pvs_zero_window_searches
      << ",\"pvs_researches\":" << r.pvs_researches << ",\"pvs_research_nodes\":" << r.pvs_research_nodes
      << ",\"aspiration_retries\":" << r.aspiration_retries
      << ",\"aspiration_fail_highs\":" << r.aspiration_fail_highs
      << ",\"aspiration_fail_lows\":" << r.aspiration_fail_lows
      << ",\"objective_time_ms\":" << r.objective_time_ms
      << ",\"style_verification_time_ms\":" << r.style_verification_time_ms
      << ",\"style_verification_max_ms\":" << r.style_verification_max_ms
      << ",\"root_style_verification_nodes\":" << r.root_style_verification_nodes
      << ",\"root_style_verification_searches\":" << r.root_style_verification_searches
      << ",\"root_style_verification_timeouts\":" << r.root_style_verification_timeouts
      << ",\"style_verification_eval_mode\":\"" << mode_name(r.style_verification_eval_mode) << "\""
      << ",\"root_style_metadata_time_us\":" << r.root_style_metadata_time_us
      << ",\"style_evaluations\":" << r.style_evaluations;
}

struct SearchRecord { std::string mode; std::string benchmark; int budget; std::string name; std::string fen; SearchResult result; double elapsed_ms; };

SearchRecord run_search(const Position& position, EvalMode mode, int depth, std::optional<int> time_ms) {
  clear_transposition_table();
  clear_search_heuristics();
  SearchLimits limits;
  limits.max_depth = time_ms ? 64 : depth;
  limits.eval_mode = mode;
  limits.profile_style_metadata = true;
  if (time_ms) {
    limits.has_deadline = true;
    limits.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(*time_ms);
  }
  const auto started = std::chrono::steady_clock::now();
  const SearchResult result = search(position.board, limits);
  return {mode_name(mode), time_ms ? "fixed_time" : "fixed_depth", time_ms.value_or(depth),
          position.name, position.fen, result, milliseconds_since(started)};
}

struct EvalMicrobench {
  std::string mode;
  bool available;
  std::string reason;
  std::uint64_t evaluations_per_sample{};
  std::uint64_t total_evaluations{};
  std::vector<double> sample_elapsed_ms;
  std::vector<double> samples_us;
  double total_elapsed_ms{};
  double median_us{};
  std::uint64_t checksum{};
};

EvalMicrobench run_evaluator_microbench(const std::vector<Position>& positions, EvalMode mode,
                                        const Options& options, const std::string& unavailable_reason) {
  EvalMicrobench result{mode_name(mode), eval_mode_available(mode), unavailable_reason};
  if (!result.available) return result;
  std::uint64_t checksum = 0;
  auto evaluate_once = [&](const Board& board) {
    const auto score = evaluate(board, mode);
    if (!score) throw std::runtime_error("evaluator became unavailable during benchmark");
    checksum += static_cast<std::uint64_t>(static_cast<std::int64_t>(*score) + 32768);
  };
  for (int iteration = 0; iteration < options.eval_warmup; ++iteration)
    for (const Position& position : positions) evaluate_once(position.board);
  result.evaluations_per_sample = static_cast<std::uint64_t>(options.eval_iterations) * positions.size();
  result.total_evaluations = result.evaluations_per_sample * options.eval_samples;
  for (int sample = 0; sample < options.eval_samples; ++sample) {
    const auto started = std::chrono::steady_clock::now();
    for (int iteration = 0; iteration < options.eval_iterations; ++iteration)
      for (const Position& position : positions) evaluate_once(position.board);
    const double elapsed_ms = milliseconds_since(started);
    result.sample_elapsed_ms.push_back(elapsed_ms);
    result.total_elapsed_ms += elapsed_ms;
    result.samples_us.push_back(elapsed_ms * 1000.0 / result.evaluations_per_sample);
  }
  std::sort(result.samples_us.begin(), result.samples_us.end());
  result.median_us = result.samples_us[result.samples_us.size() / 2];
  result.checksum = checksum;
  return result;
}

void write_json(const Options& options, const std::vector<Position>& positions,
                const std::vector<SearchRecord>& records, const std::vector<EvalMicrobench>& micros,
                bool network_loaded, const std::string& network_status) {
  ensure_parent_directory(options.output);
  std::ofstream out(options.output);
  if (!out) throw std::runtime_error("cannot write JSON output: " + options.output.string());
  out << std::setprecision(12);
  out << "{\n\"schema\":\"hebichess-phase6-search-baseline-v1\",\n"
      << "\"network_loaded\":" << (network_loaded ? "true" : "false")
      << ",\n\"network_status\":\"" << json_escape(network_status) << "\",\n\"positions\":[";
  for (std::size_t i = 0; i < positions.size(); ++i) {
    if (i) out << ',';
    out << "{\"name\":\"" << json_escape(positions[i].name) << "\",\"fen\":\""
        << json_escape(positions[i].fen) << "\"}";
  }
  out << "],\n\"search\":[\n";
  for (std::size_t i = 0; i < records.size(); ++i) {
    const auto& item = records[i];
    if (i) out << ",\n";
    out << "{\"eval_mode\":\"" << item.mode << "\",\"benchmark\":\"" << item.benchmark
        << "\",\"budget\":" << item.budget << ",\"name\":\"" << json_escape(item.name)
        << "\",\"fen\":\"" << json_escape(item.fen) << "\",\"elapsed_ms\":" << item.elapsed_ms
        << ",\"nps\":" << item.result.nodes * 1000.0 / std::max(item.elapsed_ms, 0.001) << ',';
    write_search_fields(out, item.result);
    out << '}';
  }
  out << "\n],\n\"evaluator_microbench\":[";
  for (std::size_t i = 0; i < micros.size(); ++i) {
    if (i) out << ',';
    const auto& item = micros[i];
    out << "{\"eval_mode\":\"" << item.mode << "\",\"available\":" << (item.available ? "true" : "false")
        << ",\"reason\":\"" << json_escape(item.reason) << "\",\"evaluations_per_sample\":" << item.evaluations_per_sample
        << ",\"total_evaluations\":" << item.total_evaluations << ",\"total_elapsed_ms\":" << item.total_elapsed_ms
        << ",\"median_us_per_evaluation\":" << item.median_us << ",\"checksum\":" << item.checksum << ",\"sample_elapsed_ms\":[";
    for (std::size_t sample = 0; sample < item.sample_elapsed_ms.size(); ++sample) { if (sample) out << ','; out << item.sample_elapsed_ms[sample]; }
    out << "],\"samples_us_per_evaluation\":[";
    for (std::size_t sample = 0; sample < item.samples_us.size(); ++sample) { if (sample) out << ','; out << item.samples_us[sample]; }
    out << "]}";
  }
  const auto hce = std::find_if(micros.begin(), micros.end(), [](const auto& item) { return item.mode == "HCE"; });
  const auto nnue = std::find_if(micros.begin(), micros.end(), [](const auto& item) { return item.mode == "NNUE"; });
  out << "],\n\"nnue_hce_slowdown_ratio\":";
  if (hce != micros.end() && nnue != micros.end() && hce->available && nnue->available && hce->median_us > 0)
    out << nnue->median_us / hce->median_us;
  else out << "null";
  out << "\n}\n";
}

void write_summary(const Options& options, const std::vector<SearchRecord>& records,
                   const std::vector<EvalMicrobench>& micros, bool network_loaded,
                   const std::string& network_status) {
  ensure_parent_directory(options.summary);
  std::ofstream out(options.summary);
  if (!out) throw std::runtime_error("cannot write summary: " + options.summary.string());
  out << std::fixed << std::setprecision(3) << "HebiChess Phase 6-1 search baseline\n"
      << "network: " << (network_loaded ? "loaded" : "unavailable") << " (" << network_status << ")\n\n";
  for (const auto& item : records) {
    out << item.benchmark << " budget=" << item.budget << " mode=" << item.mode << " " << item.name
        << " depth=" << item.result.completed_depth << " bestmove=" << move_to_uci(item.result.best_move)
        << " score=" << item.result.score << " elapsed_ms=" << item.elapsed_ms << " nodes=" << item.result.nodes
        << " nps=" << item.result.nodes * 1000.0 / std::max(item.elapsed_ms, 0.001)
        << " qshare=" << ratio(item.result.qnodes, item.result.nodes)
        << " tt_hit_rate=" << ratio(item.result.tt_hits, item.result.tt_probes)
        << " style_ms=" << item.result.style_verification_time_ms << '\n';
  }
  out << "\nEvaluator microbench (median us/evaluation)\n";
  for (const auto& item : micros) {
    out << item.mode << ": " << (item.available ? std::to_string(item.median_us) : "unavailable")
        << (item.available ? "" : " (" + item.reason + ")") << '\n';
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    const Options options = parse_options(argc, argv);
    const std::vector<Position> positions = load_positions(options.fixture);
    bool wants_nnue = std::find(options.modes.begin(), options.modes.end(), EvalMode::NNUE) != options.modes.end();
    bool network_loaded = false;
    std::string network_status = "not requested";
    if (wants_nnue) {
      if (!options.network) network_status = "NNUE unavailable: pass --network <frozen-model-path>";
      else {
        std::string error;
        network_loaded = load_nnue_network(options.network->string(), error);
        network_status = network_loaded ? "loaded " + options.network->string() : "NNUE unavailable: " + error;
      }
    }
    std::vector<SearchRecord> records;
    for (EvalMode mode : options.modes) {
      if (mode == EvalMode::NNUE && !network_loaded) continue;
      for (const Position& position : positions) records.push_back(run_search(position, mode, options.depth, std::nullopt));
      for (int time_ms : options.time_ms)
        for (const Position& position : positions) records.push_back(run_search(position, mode, options.depth, time_ms));
    }
    std::vector<EvalMicrobench> micros;
    for (EvalMode mode : options.modes)
      micros.push_back(run_evaluator_microbench(positions, mode, options, mode == EvalMode::NNUE ? network_status : ""));
    write_json(options, positions, records, micros, network_loaded, network_status);
    write_summary(options, records, micros, network_loaded, network_status);
    std::cout << "wrote " << options.output << " and " << options.summary << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "search baseline error: " << error.what() << '\n';
    return 2;
  }
}
