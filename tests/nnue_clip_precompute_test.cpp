#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "chess/nnue.hpp"
#include "chess/nnue_features.hpp"

using namespace hebichess;

namespace {

constexpr std::uint32_t kAccumulator = 256;
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
  std::cerr << "NNUE clip-precompute test failure: " << message << '\n';
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

std::vector<std::uint8_t> deterministic_v3_network() {
  std::vector<float> parameters;
  parameters.reserve(static_cast<std::size_t>(kParameterCount));
  std::uint32_t state = 20260916U;
  const auto append = [&](std::size_t count, float limit) {
    for (std::size_t i = 0; i < count; ++i) parameters.push_back(next_value(state, limit));
  };
  append(kNnueInputDimensions * kAccumulator, 0.001F);
  for (std::size_t i = 0; i < kAccumulator; ++i) parameters.push_back(0.25F + next_value(state, 0.01F));
  append(static_cast<std::size_t>(kHidden1) * 2 * kAccumulator, 0.02F);
  for (std::size_t i = 0; i < kHidden1; ++i) parameters.push_back(0.05F + next_value(state, 0.01F));
  append(static_cast<std::size_t>(kHidden2) * kHidden1, 0.04F);
  for (std::size_t i = 0; i < kHidden2; ++i) parameters.push_back(0.20F + next_value(state, 0.01F));
  append(kHidden2, 100.0F);
  parameters.push_back(next_value(state, 10.0F));
  require(parameters.size() == kParameterCount, "deterministic parameter count");

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
  require(static_cast<bool>(input), "open 100-FEN parity corpus");
  std::vector<Board> positions;
  std::string fen;
  while (std::getline(input, fen)) {
    if (fen.empty() || fen.front() == '#') continue;
    const auto board = Board::from_fen(fen);
    require(board.has_value(), "valid FEN in parity corpus");
    positions.push_back(*board);
  }
  require(positions.size() == 100, "exactly 100 parity positions");
  return positions;
}

volatile double benchmark_sink = 0.0;

template <typename Evaluator>
double benchmark(const std::vector<Board>& positions, Evaluator evaluate) {
  constexpr std::size_t kIterations = 20;
  double sum = 0.0;
  const auto started = std::chrono::steady_clock::now();
  for (std::size_t repeat = 0; repeat < kIterations; ++repeat) {
    for (const Board& board : positions) {
      const auto score = evaluate(board);
      require(score.has_value(), "raw score available during benchmark");
      sum += *score;
    }
  }
  benchmark_sink = benchmark_sink + sum;
  const auto elapsed = std::chrono::steady_clock::now() - started;
  return std::chrono::duration<double, std::micro>(elapsed).count() /
      static_cast<double>(kIterations * positions.size());
}

void test_raw_score_parity_and_perf() {
  const std::vector<std::uint8_t> network = deterministic_v3_network();
  std::string error;
  require(load_nnue_network_bytes(network.data(), network.size(), error), error.c_str());
  const std::vector<Board> positions = load_positions();

  float max_difference = 0.0F;
  for (const Board& board : positions) {
    const auto clipped = evaluate_nnue_network_raw(board);
    const auto legacy = evaluate_nnue_network_raw_reference(board);
    require(clipped.has_value() && legacy.has_value(), "raw score available");
    max_difference = std::max(max_difference, std::fabs(*clipped - *legacy));
  }
  require(max_difference < 0.001F, "raw NNUE score difference must be below 0.001 cp");

  const double legacy_us = benchmark(positions, evaluate_nnue_network_raw_reference);
  const double clipped_us = benchmark(positions, evaluate_nnue_network_raw);
  const double relative_speedup = (legacy_us / clipped_us - 1.0) * 100.0;
  clear_nnue_network();
  std::cout << std::fixed << std::setprecision(6)
            << "NNUE clip-precompute parity: positions=" << positions.size()
            << " max_abs_diff_cp=" << max_difference << '\n'
            << "NNUE synthetic perf: legacy_us_per_eval=" << legacy_us
            << " clip_precompute_us_per_eval=" << clipped_us
            << " relative_speedup_pct=" << relative_speedup << '\n';
}

}  // namespace

int main() {
  test_raw_score_parity_and_perf();
  return 0;
}
