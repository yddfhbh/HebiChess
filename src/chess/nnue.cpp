#include "chess/nnue.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

#include "chess/nnue_features.hpp"

namespace hebichess {
namespace {
constexpr std::uint32_t kVersion = 1, kAccumulator = 256, kHidden1 = 32, kHidden2 = 32;
constexpr std::uint64_t kParameterCount =
    kNnueInputDimensions * kAccumulator + kAccumulator + kHidden1 * (2 * kAccumulator) +
    kHidden1 + kHidden2 * kHidden1 + kHidden2 + kHidden2 + 1;

#pragma pack(push, 1)
struct Header {
  char magic[8];
  std::uint32_t version, feature_set, input_dimensions, accumulator_dimensions;
  std::uint32_t hidden1_dimensions, hidden2_dimensions, scalar_type, endian_marker;
  std::uint64_t parameter_count, checksum;
};
#pragma pack(pop)
static_assert(sizeof(Header) == 56);

struct Network {
  std::vector<float> transform;
  std::array<float, kAccumulator> transform_bias{};
  std::vector<float> hidden1;
  std::array<float, kHidden1> hidden1_bias{};
  std::array<float, kHidden2 * kHidden1> hidden2{};
  std::array<float, kHidden2> hidden2_bias{};
  std::array<float, kHidden2> output{};
  float output_bias{0};
};

std::optional<Network>& loaded_network() { static std::optional<Network> value; return value; }

std::uint64_t fnv1a(const std::vector<float>& parameters) noexcept {
  std::uint64_t hash = 1469598103934665603ULL;
  const auto* bytes = reinterpret_cast<const unsigned char*>(parameters.data());
  for (std::size_t i = 0; i < parameters.size() * sizeof(float); ++i) {
    hash ^= bytes[i]; hash *= 1099511628211ULL;
  }
  return hash;
}

float clipped_relu(float value) noexcept { return std::clamp(value, 0.0F, 1.0F); }

}  // namespace

bool load_nnue_network(const std::string& path, std::string& error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) { error = "cannot open NNUE file: " + path; return false; }
  Header header{};
  file.read(reinterpret_cast<char*>(&header), sizeof(header));
  if (file.gcount() != static_cast<std::streamsize>(sizeof(header))) { error = "NNUE file is truncated (header)"; return false; }
  if (std::memcmp(header.magic, "HEBINNUE", 8) != 0) { error = "NNUE bad magic"; return false; }
  if (header.version != kVersion) { error = "NNUE unsupported format version"; return false; }
  if (header.feature_set != kNnueFeatureSetV1 || header.input_dimensions != kNnueInputDimensions ||
      header.accumulator_dimensions != kAccumulator || header.hidden1_dimensions != kHidden1 ||
      header.hidden2_dimensions != kHidden2) { error = "NNUE incompatible feature/network dimensions"; return false; }
  if (header.scalar_type != 1 || header.endian_marker != 0x01020304U || header.parameter_count != kParameterCount) {
    error = "NNUE unsupported scalar type, endian marker, or parameter count"; return false;
  }
  std::vector<float> values(static_cast<std::size_t>(kParameterCount));
  file.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(float)));
  if (file.gcount() != static_cast<std::streamsize>(values.size() * sizeof(float))) { error = "NNUE file is truncated (parameters)"; return false; }
  char extra;
  if (file.read(&extra, 1)) { error = "NNUE file has trailing data"; return false; }
  if (fnv1a(values) != header.checksum) { error = "NNUE checksum mismatch"; return false; }
  Network network;
  std::size_t offset = 0;
  auto take = [&](auto& destination) { const std::size_t count = destination.size(); std::copy_n(values.data() + offset, count, destination.data()); offset += count; };
  network.transform.resize(kNnueInputDimensions * kAccumulator); take(network.transform);
  take(network.transform_bias); network.hidden1.resize(kHidden1 * 2 * kAccumulator); take(network.hidden1);
  take(network.hidden1_bias); take(network.hidden2); take(network.hidden2_bias); take(network.output);
  network.output_bias = values[offset];
  loaded_network() = std::move(network);
  return true;
}

bool nnue_network_available() noexcept { return loaded_network().has_value(); }
void clear_nnue_network() noexcept { loaded_network().reset(); }

std::optional<float> evaluate_nnue_network_raw(const Board& board) noexcept {
  const auto& maybe = loaded_network(); if (!maybe) return std::nullopt;
  const Network& n = *maybe;
  std::array<float, kAccumulator> white = n.transform_bias, black = n.transform_bias;
  const auto add = [&](const NnueFeatures& features, auto& accumulator) {
    for (std::size_t f = 0; f < features.size; ++f) {
      const float* row = n.transform.data() + static_cast<std::size_t>(features.indices[f]) * kAccumulator;
      for (std::size_t i = 0; i < kAccumulator; ++i) accumulator[i] += row[i];
    }
  };
  add(extract_nnue_features(board, Color::White), white);
  add(extract_nnue_features(board, Color::Black), black);
  const auto& stm = board.side_to_move() == Color::White ? white : black;
  const auto& opp = board.side_to_move() == Color::White ? black : white;
  std::array<float, kHidden1> h1{};
  for (std::size_t o = 0; o < kHidden1; ++o) {
    float sum = n.hidden1_bias[o]; const float* row = n.hidden1.data() + o * 2 * kAccumulator;
    for (std::size_t i = 0; i < kAccumulator; ++i) sum += row[i] * clipped_relu(stm[i]);
    for (std::size_t i = 0; i < kAccumulator; ++i) sum += row[kAccumulator + i] * clipped_relu(opp[i]);
    h1[o] = clipped_relu(sum);
  }
  std::array<float, kHidden2> h2{};
  for (std::size_t o = 0; o < kHidden2; ++o) { float sum = n.hidden2_bias[o]; for (std::size_t i = 0; i < kHidden1; ++i) sum += n.hidden2[o * kHidden1 + i] * h1[i]; h2[o] = clipped_relu(sum); }
  float score = n.output_bias; for (std::size_t i = 0; i < kHidden2; ++i) score += n.output[i] * h2[i];
  if (!std::isfinite(score)) return std::nullopt;
  return score;
}

std::optional<int> evaluate_nnue_network(const Board& board) noexcept {
  const auto score = evaluate_nnue_network_raw(board);
  if (!score) return std::nullopt;
  return static_cast<int>(std::lround(*score));
}

}  // namespace hebichess
