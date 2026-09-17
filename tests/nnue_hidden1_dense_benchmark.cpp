#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
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

void require(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "NNUE hidden1 dense benchmark failure: " << message << '\n';
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

std::vector<Board> load_positions() {
  std::ifstream input("tests/data/wasm-parity-100.fen");
  require(static_cast<bool>(input), "open frozen 100-position corpus");
  std::vector<Board> positions;
  std::string fen;
  while (std::getline(input, fen)) {
    if (fen.empty() || fen.front() == '#') continue;
    const auto board = Board::from_fen(fen);
    require(board.has_value(), "valid frozen corpus FEN");
    positions.push_back(*board);
  }
  require(positions.size() == 100, "exactly 100 frozen positions");
  return positions;
}

double median(std::array<double, 5> values) {
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

volatile float sink = 0.0F;

template <typename Operation>
double measure(const std::vector<Board>& boards, std::size_t repeats, Operation operation) {
  float sum = 0.0F;
  const auto started = std::chrono::steady_clock::now();
  for (std::size_t repeat = 0; repeat < repeats; ++repeat)
    for (const Board& board : boards) sum += operation(board);
  const auto elapsed = std::chrono::steady_clock::now() - started;
  sink = sink + sum;
  return std::chrono::duration<double, std::micro>(elapsed).count() /
      static_cast<double>(boards.size() * repeats);
}

struct ComposedSample {
  Board parent;
  Board child;
  Move move;
  NnueAccumulator parent_accumulator;
};

std::vector<ComposedSample> composed_samples(const std::vector<Board>& boards) {
  std::vector<ComposedSample> samples;
  samples.reserve(boards.size());
  for (const Board& board : boards) {
    Board probe = board;
    const std::vector<Move> legal = generate_legal_moves(probe);
    require(!legal.empty(), "frozen corpus position has legal move");
    ComposedSample sample{board, board, legal.front(), {}};
    require(refresh_nnue_accumulator(sample.parent, sample.parent_accumulator),
            "refresh composed parent accumulator");
    sample.child.make_move(sample.move);
    samples.push_back(std::move(sample));
  }
  return samples;
}

double measure_composed(const std::vector<ComposedSample>& samples, std::size_t repeats) {
  float sum = 0.0F;
  const auto started = std::chrono::steady_clock::now();
  for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
    for (const ComposedSample& sample : samples) {
      NnueAccumulator child_accumulator;
      require(update_nnue_accumulator(sample.parent, sample.move, sample.parent_accumulator,
                                      child_accumulator), "incremental composed update");
      const auto score = evaluate_nnue_network_raw_from_accumulator(sample.child, child_accumulator);
      require(score.has_value(), "composed raw score");
      sum += *score;
    }
  }
  const auto elapsed = std::chrono::steady_clock::now() - started;
  sink = sink + sum;
  return std::chrono::duration<double, std::micro>(elapsed).count() /
      static_cast<double>(samples.size() * repeats);
}

void check_parity(const std::vector<Board>& boards, float& max_hidden1,
                  float& max_raw) {
  for (const Board& board : boards) {
    const auto active_hidden1 = evaluate_nnue_hidden1_pre_active_for_test(board);
    const auto legacy_hidden1 = evaluate_nnue_hidden1_pre_legacy_for_test(board);
    require(active_hidden1.has_value() && legacy_hidden1.has_value(), "hidden1 test probe");
    require(active_hidden1->size() == legacy_hidden1->size(), "hidden1 probe dimensions");
    for (std::size_t i = 0; i < active_hidden1->size(); ++i)
      max_hidden1 = std::max(max_hidden1, std::fabs((*active_hidden1)[i] - (*legacy_hidden1)[i]));
    const auto active = evaluate_nnue_network_raw(board);
    const auto legacy = evaluate_nnue_network_raw_reference(board);
    require(active.has_value() && legacy.has_value(), "raw parity score");
    max_raw = std::max(max_raw, std::fabs(*active - *legacy));
  }
  require(max_hidden1 == 0.0F, "hidden1 must be bit-identical to legacy");
  require(max_raw == 0.0F, "raw score must be bit-identical to legacy");
}

}  // namespace

int main() {
  const std::vector<std::uint8_t> network = deterministic_network();
  std::string error;
  require(load_nnue_network_bytes(network.data(), network.size(), error), error.c_str());
  const std::vector<Board> boards = load_positions();
  float max_hidden1 = 0.0F;
  float max_raw = 0.0F;
  check_parity(boards, max_hidden1, max_raw);
  const std::vector<ComposedSample> composed = composed_samples(boards);

  std::array<double, 5> hidden1{};
  std::array<double, 5> full{};
  std::array<double, 5> composed_us{};
  for (std::size_t round = 0; round < 5; ++round) {
    const auto stages = profile_nnue_evaluator_stages(boards, 128);
    require(stages.has_value(), "hidden1 stage profile");
    hidden1[round] = stages->hidden1_dense_us;
    full[round] = measure(boards, 128, [](const Board& board) {
      const auto score = evaluate_nnue_network_raw(board);
      require(score.has_value(), "full raw score");
      return *score;
    });
    composed_us[round] = measure_composed(composed, 128);
  }
  clear_nnue_network();
  std::cout << std::fixed << std::setprecision(6)
            << "NNUE hidden1 dense variant=" << HEBICHESS_NNUE_HIDDEN1_VARIANT
            << " positions=" << boards.size()
            << " hidden1_median_us=" << median(hidden1)
            << " full_median_us=" << median(full)
            << " composed_median_us=" << median(composed_us)
            << " hidden1_max_abs_diff=" << max_hidden1
            << " final_raw_max_abs_diff=" << max_raw
            << " final_cp_max_abs_diff=" << max_raw << '\n';
  return 0;
}
