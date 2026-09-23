#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "chess/movegen.hpp"
#include "chess/nnue.hpp"
#include "chess/search.hpp"
#include "chess/uci_engine.hpp"
#include "chess/game_state.hpp"

#ifdef HEBICHESS_BROWSER_WASM
#include <emscripten.h>

EM_JS(void, hebichess_browser_output, (const char* line), {
  if (typeof self.hebichessOutputLine === 'function') {
    self.hebichessOutputLine(UTF8ToString(line));
  }
});
#endif

namespace {

std::string output;
hebichess::GameState game_state;
hebichess::UciEngine& engine() {
  static hebichess::UciEngine instance([](const std::string& line) {
    output += line;
    output += '\n';
#ifdef HEBICHESS_BROWSER_WASM
    hebichess_browser_output(line.c_str());
#endif
  });
  return instance;
}

#ifdef HEBICHESS_WASM_H1_AB_BENCHMARK
struct NnueBenchmarkTransition {
  hebichess::Board parent;
  hebichess::Board child;
  hebichess::Move move;
  hebichess::NnueAccumulator parent_accumulator;
};

volatile float nnue_benchmark_sink = 0.0F;

template <typename Operation>
double nnue_benchmark_median_us(std::size_t evaluations, std::size_t repeats, Operation operation) {
  std::array<double, 5> samples{};
  for (double& sample : samples) {
    float sum = 0.0F;
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t repeat = 0; repeat < repeats; ++repeat) sum += operation();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    nnue_benchmark_sink = nnue_benchmark_sink + sum;
    sample = std::chrono::duration<double, std::micro>(elapsed).count() /
        static_cast<double>(evaluations * repeats);
  }
  std::sort(samples.begin(), samples.end());
  return samples[samples.size() / 2];
}

const char* nnue_benchmark_error(const char* message) {
  output = std::string("{\"ok\":false,\"error\":\"") + message + "\"}";
  return output.c_str();
}
#endif

}  // namespace

extern "C" {

void hebichess_initialize() {
  output.clear();
  engine().send_command("uci");
  engine().send_command("isready");
}

void hebichess_send_command(const char* command) {
  output.clear();
  if (!command) {
    output = "info string error empty command\n";
    return;
  }
  engine().send_command(command);
}

void hebichess_game_reset() { game_state.reset(); }
int hebichess_game_load_fen(const char* fen) { return fen && game_state.load_fen(fen) ? 1 : 0; }
const char* hebichess_game_fen() { output = game_state.fen(); return output.c_str(); }
const char* hebichess_game_legal_moves() {
  output = "[";
  const auto moves = game_state.legal_moves();
  for (std::size_t i = 0; i < moves.size(); ++i) { if (i) output += ','; output += '"' + moves[i] + '"'; }
  output += ']'; return output.c_str();
}
const char* hebichess_game_apply(const char* move) {
  std::string san; hebichess::GameStateStatus status;
  if (!move || !game_state.apply_uci(move, san, status)) { output = "{\"ok\":false}"; return output.c_str(); }
  output = "{\"ok\":true,\"fen\":\"" + game_state.fen() + "\",\"san\":\"" + san +
           "\",\"status\":\"" + status.status + "\",\"result\":\"" + status.result + "\"}";
  return output.c_str();
}
const char* hebichess_game_status() {
  const auto status = game_state.status();
  output = "{\"status\":\"" + status.status + "\",\"result\":\"" + status.result + "\"}";
  return output.c_str();
}

const char* hebichess_take_output() {
  return output.c_str();
}

int hebichess_nnue_load_bytes(const std::uint8_t* bytes, std::size_t size) {
  output.clear();
  std::string error;
  if (!hebichess::load_nnue_network_bytes(bytes, size, error)) {
    output = "info string error " + error + "\n";
    return 0;
  }
  output = "info string NNUE network loaded browser binary\n";
  return 1;
}

int hebichess_book_load_bytes(const std::uint8_t* bytes, std::size_t size) {
  output.clear();
  if (!bytes || !engine().load_opening_book_bytes({bytes, size})) {
    engine().clear_opening_book();
    output = "info string error opening book load failed\n";
    return 0;
  }
  output = "info string opening book loaded browser binary\n";
  return 1;
}

void hebichess_book_clear() {
  engine().clear_opening_book();
  output = "info string opening book cleared\n";
}

void hebichess_nnue_clear() {
  hebichess::clear_nnue_network();
  output = "info string NNUE network cleared\n";
}

const char* hebichess_nnue_evaluate_fen_raw(const char* fen) {
  output.clear();
  if (!fen) { output = "error empty FEN"; return output.c_str(); }
  const auto board = hebichess::Board::from_fen(fen);
  if (!board) { output = "error invalid FEN"; return output.c_str(); }
  const auto score = hebichess::evaluate_nnue_network_raw(*board);
  if (!score) { output = "error NNUE unavailable"; return output.c_str(); }
  std::ostringstream value;
  value << std::setprecision(std::numeric_limits<float>::max_digits10) << *score;
  output = value.str();
  return output.c_str();
}

const char* hebichess_qsearch_move_buffer_layout() {
  const hebichess::QsearchMoveBufferLayout layout = hebichess::qsearch_move_buffer_layout();
  std::ostringstream value;
  value << "{\"sizeof_move\":" << layout.move_size
        << ",\"sizeof_ordered_move\":" << layout.ordered_move_size
        << ",\"sizeof_fixed_move_list\":" << layout.fixed_move_list_size
        << ",\"sizeof_fixed_ordered_move_list\":" << layout.fixed_ordered_move_list_size
        << ",\"simultaneously_live_buffer_bytes\":" << layout.simultaneously_live_buffer_bytes
        << ",\"fixed_move_list_enabled\":"
        << (layout.fixed_move_list_enabled ? "true" : "false")
        << ",\"fixed_and_ordered_buffers_simultaneously_live\":"
        << (layout.fixed_and_ordered_buffers_simultaneously_live ? "true" : "false")
        << '}';
  output = value.str();
  return output.c_str();
}

#ifdef HEBICHESS_WASM_H1_AB_BENCHMARK
// This test-only ABI keeps the timed loops inside WASM.  JavaScript only
// supplies the frozen corpus and receives a small JSON result, so the result
// measures existing-accumulator evaluation and update+evaluation rather than
// repeated JS/C++ boundary crossings.
const char* hebichess_nnue_benchmark_fens(const char* corpus, int repeats) {
  if (!hebichess::nnue_network_available()) return nnue_benchmark_error("NNUE unavailable");
  if (!corpus) return nnue_benchmark_error("empty corpus");
  if (repeats <= 0) return nnue_benchmark_error("repeats must be positive");

  std::vector<hebichess::Board> boards;
  std::istringstream input(corpus);
  std::string fen;
  while (std::getline(input, fen)) {
    if (!fen.empty() && fen.back() == '\r') fen.pop_back();
    if (fen.empty() || fen.front() == '#') continue;
    const auto board = hebichess::Board::from_fen(fen);
    if (!board) return nnue_benchmark_error("invalid corpus FEN");
    boards.push_back(*board);
  }
  if (boards.empty()) return nnue_benchmark_error("empty corpus");

  std::vector<hebichess::NnueAccumulator> accumulators(boards.size());
  for (std::size_t index = 0; index < boards.size(); ++index) {
    if (!hebichess::refresh_nnue_accumulator(boards[index], accumulators[index]))
      return nnue_benchmark_error("accumulator rebuild failed");
  }

  std::vector<NnueBenchmarkTransition> transitions;
  for (std::size_t index = 0; index < boards.size(); ++index) {
    for (const hebichess::Move& move : hebichess::generate_legal_moves(boards[index])) {
      hebichess::Board child = boards[index];
      child.make_move(move);
      transitions.push_back({boards[index], std::move(child), move, accumulators[index]});
    }
  }
  if (transitions.empty()) return nnue_benchmark_error("corpus has no legal transitions");

  // Warm-up stays outside all five median samples.
  for (std::size_t index = 0; index < boards.size(); ++index) {
    if (!hebichess::evaluate_nnue_network_raw_from_accumulator(boards[index], accumulators[index]))
      return nnue_benchmark_error("existing accumulator evaluation failed");
  }
  for (const NnueBenchmarkTransition& sample : transitions) {
    hebichess::NnueAccumulator child_accumulator;
    if (!hebichess::update_nnue_accumulator(sample.parent, sample.move,
                                            sample.parent_accumulator, child_accumulator) ||
        !hebichess::evaluate_nnue_network_raw_from_accumulator(sample.child, child_accumulator))
      return nnue_benchmark_error("incremental update evaluation failed");
  }

  const std::size_t repeat_count = static_cast<std::size_t>(repeats);
  const double existing_us = nnue_benchmark_median_us(boards.size(), repeat_count, [&] {
    float sum = 0.0F;
    for (std::size_t index = 0; index < boards.size(); ++index) {
      const auto score = hebichess::evaluate_nnue_network_raw_from_accumulator(boards[index], accumulators[index]);
      if (!score) return std::numeric_limits<float>::quiet_NaN();
      sum += *score;
    }
    return sum;
  });
  const double incremental_us = nnue_benchmark_median_us(transitions.size(), repeat_count, [&] {
    float sum = 0.0F;
    for (const NnueBenchmarkTransition& sample : transitions) {
      hebichess::NnueAccumulator child_accumulator;
      if (!hebichess::update_nnue_accumulator(sample.parent, sample.move,
                                              sample.parent_accumulator, child_accumulator))
        return std::numeric_limits<float>::quiet_NaN();
      const auto score = hebichess::evaluate_nnue_network_raw_from_accumulator(sample.child, child_accumulator);
      if (!score) return std::numeric_limits<float>::quiet_NaN();
      sum += *score;
    }
    return sum;
  });
  if (!std::isfinite(existing_us) || !std::isfinite(incremental_us))
    return nnue_benchmark_error("benchmark evaluation failed");

  output.clear();
  std::ostringstream result;
  result << std::setprecision(12)
         << "{\"ok\":true,\"positions\":" << boards.size()
         << ",\"transitions\":" << transitions.size()
         << ",\"repeats\":" << repeats
         << ",\"samples\":5"
         << ",\"existing_accumulator_evaluate_us_per_eval\":" << existing_us
         << ",\"incremental_update_evaluate_us_per_eval\":" << incremental_us
         << '}';
  output = result.str();
  return output.c_str();
}
#endif

}
