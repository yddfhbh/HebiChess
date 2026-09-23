#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "chess/movegen.hpp"
#include "chess/nnue.hpp"
#include "chess/nnue_features.hpp"

using namespace hebichess;

namespace {

constexpr std::uint32_t kAccumulator = kNnueAccumulatorDimensions;
constexpr std::uint32_t kHidden1 = 128;
constexpr std::uint32_t kHidden2 = 128;
constexpr std::uint64_t kParameterCount =
    static_cast<std::uint64_t>(kNnueInputDimensions) * kAccumulator + kAccumulator +
    static_cast<std::uint64_t>(kHidden1) * 2 * kAccumulator + kHidden1 +
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
static_assert(sizeof(HeaderV3) == 68);

struct Options {
  std::optional<std::string> network_path;
  std::string corpus_path{"tests/data/wasm-parity-100.fen"};
  std::size_t repeats{128};
  bool verify_only{false};
};

void fail(const std::string& message) {
  std::cerr << "NNUE evaluator benchmark failure: " << message << '\n';
  std::exit(2);
}

void require(bool condition, const std::string& message) {
  if (!condition) fail(message);
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
  std::uint32_t state = 20260916U;
  const auto append = [&](std::size_t count, float limit) {
    for (std::size_t i = 0; i < count; ++i) parameters.push_back(next_value(state, limit));
  };
  append(kNnueInputDimensions * kAccumulator, 0.001F);
  for (std::size_t i = 0; i < kAccumulator; ++i)
    parameters.push_back(0.25F + next_value(state, 0.01F));
  append(static_cast<std::size_t>(kHidden1) * 2 * kAccumulator, 0.02F);
  for (std::size_t i = 0; i < kHidden1; ++i)
    parameters.push_back(0.05F + next_value(state, 0.01F));
  append(static_cast<std::size_t>(kHidden2) * kHidden1, 0.04F);
  for (std::size_t i = 0; i < kHidden2; ++i)
    parameters.push_back(0.20F + next_value(state, 0.01F));
  append(kHidden2, 100.0F);
  parameters.push_back(next_value(state, 10.0F));
  const auto* payload = reinterpret_cast<const std::uint8_t*>(parameters.data());
  const std::size_t payload_size = parameters.size() * sizeof(float);
  HeaderV3 header{{'H', 'E', 'B', 'I', 'N', 'N', 'U', 'E'}, 3, kNnueFeatureSetV1,
                  static_cast<std::uint32_t>(kNnueInputDimensions), kAccumulator,
                  kHidden1, kHidden2, 1, 1, 0x01020304U, 2, 1.0F,
                  kParameterCount, fnv1a_bytes(payload, payload_size)};
  std::vector<std::uint8_t> bytes(sizeof(header) + payload_size);
  std::memcpy(bytes.data(), &header, sizeof(header));
  std::memcpy(bytes.data() + sizeof(header), payload, payload_size);
  return bytes;
}

Options parse_options(int argc, char* argv[]) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const auto value = [&]() -> std::string_view {
      if (++index >= argc) fail("missing value for " + std::string(argument));
      return argv[index];
    };
    if (argument == "--network") options.network_path = std::string(value());
    else if (argument == "--corpus") options.corpus_path = std::string(value());
    else if (argument == "--repeats") {
      try {
        options.repeats = static_cast<std::size_t>(std::stoull(std::string(value())));
      } catch (const std::exception&) {
        fail("--repeats must be a positive integer");
      }
    } else if (argument == "--verify-only") {
      options.verify_only = true;
    } else if (argument == "--help") {
      std::cout << "usage: HebiChessNnueHidden1Dense<Variant> [--network FILE]"
                   " [--corpus FILE] [--repeats N] [--verify-only]\n";
      std::exit(0);
    } else {
      fail("unknown argument: " + std::string(argument));
    }
  }
  require(options.repeats > 0, "--repeats must be a positive integer");
  return options;
}

void validate_frozen_v3_network(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  require(static_cast<bool>(input), "open --network file");
  HeaderV3 header{};
  input.read(reinterpret_cast<char*>(&header), sizeof(header));
  require(input.gcount() == static_cast<std::streamsize>(sizeof(header)),
          "--network has a complete v3 header");
  require(std::memcmp(header.magic, "HEBINNUE", 8) == 0 && header.version == 3,
          "--network must be a frozen v3 HEBINNUE file");
  require(header.feature_abi == kNnueFeatureSetV1 &&
              header.input_dimensions == kNnueInputDimensions &&
              header.accumulator_dimensions == kAccumulator &&
              header.hidden1_dimensions == kHidden1 &&
              header.hidden2_dimensions == kHidden2 && header.output_dimensions == 1,
          "--network must use the production 256/128/128 dimensions");
}

std::vector<Board> load_positions(const std::string& path) {
  std::ifstream input(path);
  require(static_cast<bool>(input), "open corpus: " + path);
  std::vector<Board> positions;
  std::string fen;
  while (std::getline(input, fen)) {
    if (fen.empty() || fen.front() == '#') continue;
    const auto board = Board::from_fen(fen);
    require(board.has_value(), "valid corpus FEN");
    positions.push_back(*board);
  }
  require(!positions.empty(), "corpus has at least one position");
  return positions;
}

enum class TransitionKind { Ordinary, Capture, KingRefresh };

struct TransitionSample {
  Board parent;
  Board child;
  Move move;
  NnueAccumulator parent_accumulator;
  TransitionKind kind;
};

TransitionKind transition_kind(const Board& parent, const Move& move) {
  if (parent.piece_at(move.from).type == PieceType::King) return TransitionKind::KingRefresh;
  if (move.flag == MoveFlag::Capture || move.flag == MoveFlag::EnPassant ||
      move.flag == MoveFlag::PromotionCapture) return TransitionKind::Capture;
  return TransitionKind::Ordinary;
}

std::vector<NnueAccumulator> build_accumulators(const std::vector<Board>& boards) {
  std::vector<NnueAccumulator> accumulators(boards.size());
  for (std::size_t index = 0; index < boards.size(); ++index)
    require(refresh_nnue_accumulator(boards[index], accumulators[index]), "rebuild accumulator");
  return accumulators;
}

std::vector<TransitionSample> build_transitions(const std::vector<Board>& boards,
                                                const std::vector<NnueAccumulator>& accumulators) {
  require(boards.size() == accumulators.size(), "parent accumulator dimensions");
  std::vector<TransitionSample> samples;
  for (std::size_t index = 0; index < boards.size(); ++index) {
    Board parent = boards[index];
    const std::vector<Move> legal = generate_legal_moves(parent);
    for (const Move& move : legal) {
      TransitionSample sample{boards[index], boards[index], move, accumulators[index],
                              transition_kind(boards[index], move)};
      sample.child.make_move(move);
      samples.push_back(std::move(sample));
    }
  }
  require(!samples.empty(), "corpus must contain a legal transition");
  return samples;
}

float max_abs_difference(const NnueAccumulator& left, const NnueAccumulator& right) {
  float maximum = 0.0F;
  for (std::size_t index = 0; index < kAccumulator; ++index) {
    maximum = std::max(maximum, std::fabs(left.white[index] - right.white[index]));
    maximum = std::max(maximum, std::fabs(left.black[index] - right.black[index]));
  }
  return maximum;
}

struct ParityResults {
  float hidden1_max_abs_diff{0.0F};
  float final_raw_max_abs_diff{0.0F};
  std::size_t final_cp_mismatch_count{0};
  float incremental_accumulator_max_abs_diff{0.0F};
  float incremental_final_raw_max_abs_diff{0.0F};
  std::size_t incremental_final_cp_mismatch_count{0};
};

ParityResults verify_parity(const std::vector<Board>& boards,
                            const std::vector<TransitionSample>& transitions) {
  ParityResults results;
  for (const Board& board : boards) {
    const auto active_hidden1 = evaluate_nnue_hidden1_pre_active_for_test(board);
    const auto legacy_hidden1 = evaluate_nnue_hidden1_pre_legacy_for_test(board);
    require(active_hidden1.has_value() && legacy_hidden1.has_value(), "hidden1 test probe");
    require(active_hidden1->size() == legacy_hidden1->size(), "hidden1 probe dimensions");
    for (std::size_t index = 0; index < active_hidden1->size(); ++index)
      results.hidden1_max_abs_diff = std::max(results.hidden1_max_abs_diff,
          std::fabs((*active_hidden1)[index] - (*legacy_hidden1)[index]));
    const auto active = evaluate_nnue_network_raw(board);
    const auto legacy = evaluate_nnue_network_raw_reference(board);
    require(active.has_value() && legacy.has_value(), "raw parity score");
    results.final_raw_max_abs_diff = std::max(results.final_raw_max_abs_diff,
                                               std::fabs(*active - *legacy));
    if (std::lround(*active) != std::lround(*legacy)) ++results.final_cp_mismatch_count;
  }
  for (const TransitionSample& sample : transitions) {
    NnueAccumulator incremental;
    require(update_nnue_accumulator(sample.parent, sample.move, sample.parent_accumulator,
                                    incremental), "incremental accumulator update");
    NnueAccumulator rebuilt;
    require(refresh_nnue_accumulator(sample.child, rebuilt), "incremental child rebuild");
    results.incremental_accumulator_max_abs_diff = std::max(
        results.incremental_accumulator_max_abs_diff, max_abs_difference(incremental, rebuilt));
    const auto active = evaluate_nnue_network_raw_from_accumulator(sample.child, incremental);
    const auto rebuilt_raw = evaluate_nnue_network_raw_from_accumulator(sample.child, rebuilt);
    require(active.has_value() && rebuilt_raw.has_value(), "incremental raw parity score");
    results.incremental_final_raw_max_abs_diff = std::max(
        results.incremental_final_raw_max_abs_diff, std::fabs(*active - *rebuilt_raw));
    if (std::lround(*active) != std::lround(*rebuilt_raw))
      ++results.incremental_final_cp_mismatch_count;
  }
  require(results.hidden1_max_abs_diff == 0.0F, "hidden1 must be bit-identical to legacy");
  require(results.final_raw_max_abs_diff == 0.0F, "raw eval must be bit-identical to legacy");
  require(results.final_cp_mismatch_count == 0, "rounded cp must match legacy");
  require(results.incremental_accumulator_max_abs_diff <= 1e-4F,
          "incremental accumulator max difference <= 1e-4");
  require(results.incremental_final_raw_max_abs_diff <= 0.001F,
          "incremental raw eval max difference <= 0.001 cp");
  require(results.incremental_final_cp_mismatch_count == 0,
          "incremental rounded cp must match rebuilt accumulator");
  return results;
}

volatile float benchmark_sink = 0.0F;

template <typename Operation>
double measure_us_per_eval(std::size_t evaluations, std::size_t repeats, Operation operation) {
  float sum = 0.0F;
  const auto started = std::chrono::steady_clock::now();
  for (std::size_t repeat = 0; repeat < repeats; ++repeat) sum += operation();
  const auto elapsed = std::chrono::steady_clock::now() - started;
  benchmark_sink = benchmark_sink + sum;
  return std::chrono::duration<double, std::micro>(elapsed).count() /
      static_cast<double>(evaluations * repeats);
}

double median(std::array<double, 5> values) {
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

template <typename Operation>
double median_of_five(Operation operation) {
  std::array<double, 5> measurements{};
  for (std::size_t round = 0; round < measurements.size(); ++round)
    measurements[round] = operation();
  return median(measurements);
}

NnueEvaluatorStageProfile median_stages(const std::vector<Board>& boards, std::size_t repeats) {
  std::array<double, 5> rebuild{}, clip{}, hidden1{}, hidden1_activation{}, hidden2{}, output{};
  for (std::size_t round = 0; round < rebuild.size(); ++round) {
    const auto stages = profile_nnue_evaluator_stages(boards, repeats);
    require(stages.has_value(), "evaluator stage profile");
    rebuild[round] = stages->accumulator_rebuild_us;
    clip[round] = stages->clip_precompute_us;
    hidden1[round] = stages->hidden1_dense_us;
    hidden1_activation[round] = stages->hidden1_activation_us;
    hidden2[round] = stages->hidden2_dense_us;
    output[round] = stages->output_us;
  }
  return NnueEvaluatorStageProfile{median(rebuild), median(clip), median(hidden1),
                                   median(hidden1_activation), median(hidden2), median(output)};
}

struct BenchmarkResults {
  NnueEvaluatorStageProfile stages{};
  double full_rebuild_evaluate_us{0.0};
  double existing_accumulator_evaluate_us{0.0};
  double incremental_update_us{0.0};
  double incremental_update_evaluate_us{0.0};
  double ordinary_update_us{0.0};
  double capture_update_us{0.0};
  double king_refresh_update_us{0.0};
  std::size_t ordinary_transitions{0};
  std::size_t capture_transitions{0};
  std::size_t king_refresh_transitions{0};
};

template <typename Operation>
double measure_transition_kind(const std::vector<TransitionSample>& transitions,
                               TransitionKind kind, std::size_t repeats, Operation operation) {
  std::size_t count = 0;
  for (const TransitionSample& sample : transitions)
    if (sample.kind == kind) ++count;
  if (count == 0) return 0.0;
  return median_of_five([&] {
    return measure_us_per_eval(count, repeats, [&] {
      float sum = 0.0F;
      for (const TransitionSample& sample : transitions) {
        if (sample.kind != kind) continue;
        sum += operation(sample);
      }
      return sum;
    });
  });
}

BenchmarkResults run_benchmark(const std::vector<Board>& boards,
                               const std::vector<NnueAccumulator>& accumulators,
                               const std::vector<TransitionSample>& transitions,
                               std::size_t repeats) {
  BenchmarkResults results;
  results.stages = median_stages(boards, repeats);
  results.full_rebuild_evaluate_us = median_of_five([&] {
    return measure_us_per_eval(boards.size(), repeats, [&] {
      float sum = 0.0F;
      for (const Board& board : boards) {
        const auto score = evaluate_nnue_network_raw(board);
        require(score.has_value(), "full rebuild evaluation");
        sum += *score;
      }
      return sum;
    });
  });
  results.existing_accumulator_evaluate_us = median_of_five([&] {
    return measure_us_per_eval(boards.size(), repeats, [&] {
      float sum = 0.0F;
      for (std::size_t index = 0; index < boards.size(); ++index) {
        const auto score = evaluate_nnue_network_raw_from_accumulator(boards[index], accumulators[index]);
        require(score.has_value(), "existing accumulator evaluation");
        sum += *score;
      }
      return sum;
    });
  });
  results.incremental_update_us = median_of_five([&] {
    return measure_us_per_eval(transitions.size(), repeats, [&] {
      float sum = 0.0F;
      for (const TransitionSample& sample : transitions) {
        NnueAccumulator accumulator;
        require(update_nnue_accumulator(sample.parent, sample.move, sample.parent_accumulator,
                                        accumulator), "timed incremental update");
        sum += accumulator.white[0];
      }
      return sum;
    });
  });
  results.incremental_update_evaluate_us = median_of_five([&] {
    return measure_us_per_eval(transitions.size(), repeats, [&] {
      float sum = 0.0F;
      for (const TransitionSample& sample : transitions) {
        NnueAccumulator accumulator;
        require(update_nnue_accumulator(sample.parent, sample.move, sample.parent_accumulator,
                                        accumulator), "timed composed update");
        const auto score = evaluate_nnue_network_raw_from_accumulator(sample.child, accumulator);
        require(score.has_value(), "timed composed evaluation");
        sum += *score;
      }
      return sum;
    });
  });
  const auto update = [](const TransitionSample& sample) {
    NnueAccumulator accumulator;
    require(update_nnue_accumulator(sample.parent, sample.move, sample.parent_accumulator,
                                    accumulator), "timed transition-class update");
    return accumulator.white[0];
  };
  results.ordinary_update_us = measure_transition_kind(transitions, TransitionKind::Ordinary,
                                                        repeats, update);
  results.capture_update_us = measure_transition_kind(transitions, TransitionKind::Capture,
                                                       repeats, update);
  results.king_refresh_update_us = measure_transition_kind(transitions,
      TransitionKind::KingRefresh, repeats, update);
  for (const TransitionSample& sample : transitions) {
    if (sample.kind == TransitionKind::Ordinary) ++results.ordinary_transitions;
    else if (sample.kind == TransitionKind::Capture) ++results.capture_transitions;
    else ++results.king_refresh_transitions;
  }
  return results;
}

void print_result(const Options& options, const std::vector<Board>& boards,
                  const std::vector<TransitionSample>& transitions,
                  const ParityResults& parity, const BenchmarkResults* benchmark) {
  std::cout << std::fixed << std::setprecision(6)
            << "NNUE_EVALUATOR_BENCHMARK"
            << " variant=" << HEBICHESS_NNUE_HIDDEN1_VARIANT
            << " network=" << (options.network_path.has_value() ? "frozen_v3" : "synthetic_v3")
            << " positions=" << boards.size()
            << " transitions=" << transitions.size()
            << " hidden1_max_abs_diff=" << parity.hidden1_max_abs_diff
            << " final_raw_max_abs_diff=" << parity.final_raw_max_abs_diff
            << " final_cp_mismatch_count=" << parity.final_cp_mismatch_count
            << " incremental_accumulator_max_abs_diff=" << parity.incremental_accumulator_max_abs_diff
            << " incremental_final_raw_max_abs_diff=" << parity.incremental_final_raw_max_abs_diff
            << " incremental_final_cp_mismatch_count="
            << parity.incremental_final_cp_mismatch_count;
  if (benchmark) {
    std::cout << " accumulator_rebuild_us_per_eval=" << benchmark->stages.accumulator_rebuild_us
              << " clip_us_per_eval=" << benchmark->stages.clip_precompute_us
              << " hidden1_dense_us_per_eval=" << benchmark->stages.hidden1_dense_us
              << " hidden1_activation_us_per_eval=" << benchmark->stages.hidden1_activation_us
              << " hidden2_dense_us_per_eval=" << benchmark->stages.hidden2_dense_us
              << " output_us_per_eval=" << benchmark->stages.output_us
              << " full_rebuild_evaluate_us_per_eval=" << benchmark->full_rebuild_evaluate_us
              << " existing_accumulator_evaluate_us_per_eval="
              << benchmark->existing_accumulator_evaluate_us
              << " incremental_update_us_per_eval=" << benchmark->incremental_update_us
              << " incremental_update_evaluate_us_per_eval="
              << benchmark->incremental_update_evaluate_us
              << " ordinary_transition_count=" << benchmark->ordinary_transitions
              << " ordinary_incremental_update_us_per_eval=" << benchmark->ordinary_update_us
              << " capture_transition_count=" << benchmark->capture_transitions
              << " capture_incremental_update_us_per_eval=" << benchmark->capture_update_us
              << " king_refresh_transition_count=" << benchmark->king_refresh_transitions
              << " king_refresh_incremental_update_us_per_eval="
              << benchmark->king_refresh_update_us;
  }
  std::cout << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
  const Options options = parse_options(argc, argv);
  std::string error;
  if (options.network_path.has_value()) {
    validate_frozen_v3_network(*options.network_path);
    require(load_nnue_network(*options.network_path, error), error);
  } else {
    const std::vector<std::uint8_t> network = deterministic_network();
    require(load_nnue_network_bytes(network.data(), network.size(), error), error);
  }
  const std::vector<Board> boards = load_positions(options.corpus_path);
  const std::vector<NnueAccumulator> accumulators = build_accumulators(boards);
  const std::vector<TransitionSample> transitions = build_transitions(boards, accumulators);
  const ParityResults parity = verify_parity(boards, transitions);
  if (options.verify_only) {
    print_result(options, boards, transitions, parity, nullptr);
  } else {
    const BenchmarkResults benchmark = run_benchmark(boards, accumulators, transitions,
                                                      options.repeats);
    print_result(options, boards, transitions, parity, &benchmark);
  }
  clear_nnue_network();
  return 0;
}
