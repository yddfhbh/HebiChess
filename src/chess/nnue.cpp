#include "chess/nnue.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

#include "chess/nnue_features.hpp"

namespace hebichess {
namespace {
constexpr std::uint32_t kV1Version = 1;
constexpr std::uint32_t kV2Version = 2;
constexpr std::uint32_t kV3Version = 3;
constexpr std::uint32_t kAccumulator = 256;
constexpr std::uint32_t kV1Hidden1 = 32, kV1Hidden2 = 32;
constexpr std::uint32_t kV2Hidden1 = 128, kV2Hidden2 = 128;
constexpr std::uint32_t kOutput = 1;
constexpr std::uint32_t kScalarFloat32 = 1;
constexpr std::uint32_t kEndianMarker = 0x01020304U;
enum class FinalHiddenActivation : std::uint32_t {
  ClippedRelu01 = 1,
  Relu = 2,
};

#pragma pack(push, 1)
struct HeaderV1 {
  char magic[8];
  std::uint32_t version, feature_abi, input_dimensions, accumulator_dimensions;
  std::uint32_t hidden1_dimensions, hidden2_dimensions, scalar_type, endian_marker;
  std::uint64_t parameter_count, checksum;
};

struct HeaderV2 {
  char magic[8];
  std::uint32_t version, feature_abi, input_dimensions, accumulator_dimensions;
  std::uint32_t hidden1_dimensions, hidden2_dimensions, output_dimensions;
  std::uint32_t scalar_type, endian_marker;
  float output_scale;
  std::uint64_t parameter_count, checksum;
};

struct HeaderV3 {
  char magic[8];
  std::uint32_t version, feature_abi, input_dimensions, accumulator_dimensions;
  std::uint32_t hidden1_dimensions, hidden2_dimensions, output_dimensions;
  std::uint32_t scalar_type, endian_marker, final_hidden_activation;
  float output_scale;
  std::uint64_t parameter_count, checksum;
};
#pragma pack(pop)
static_assert(sizeof(HeaderV1) == 56);
static_assert(sizeof(HeaderV2) == 64);
static_assert(sizeof(HeaderV3) == 68);

struct Network {
  std::uint32_t hidden1_dimensions{0};
  std::uint32_t hidden2_dimensions{0};
  FinalHiddenActivation final_hidden_activation{FinalHiddenActivation::ClippedRelu01};
  float output_scale{1.0F};
  std::vector<float> transform;
  std::vector<float> transform_bias;
  std::vector<float> hidden1;
  std::vector<float> hidden1_bias;
  std::vector<float> hidden2;
  std::vector<float> hidden2_bias;
  std::vector<float> output;
  float output_bias{0.0F};
};

std::optional<Network>& loaded_network() { static std::optional<Network> value; return value; }

std::uint64_t parameter_count(std::uint32_t hidden1, std::uint32_t hidden2) noexcept {
  return static_cast<std::uint64_t>(kNnueInputDimensions) * kAccumulator + kAccumulator +
      static_cast<std::uint64_t>(hidden1) * (2 * kAccumulator) + hidden1 +
      static_cast<std::uint64_t>(hidden2) * hidden1 + hidden2 + hidden2 + 1;
}

std::uint64_t fnv1a_bytes(const std::uint8_t* bytes, std::size_t size) noexcept {
  std::uint64_t hash = 1469598103934665603ULL;
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= bytes[i]; hash *= 1099511628211ULL;
  }
  return hash;
}

bool host_is_little_endian() noexcept {
  constexpr std::uint32_t probe = 1;
  return *reinterpret_cast<const unsigned char*>(&probe) == 1;
}

float clipped_relu(float value) noexcept { return std::clamp(value, 0.0F, 1.0F); }

float final_hidden_relu(float value, FinalHiddenActivation activation) noexcept {
  return activation == FinalHiddenActivation::ClippedRelu01 ? clipped_relu(value)
                                                            : std::max(value, 0.0F);
}

bool supported_v3_activation(std::uint32_t value) noexcept {
  return value == static_cast<std::uint32_t>(FinalHiddenActivation::ClippedRelu01) ||
         value == static_cast<std::uint32_t>(FinalHiddenActivation::Relu);
}

bool supported_v3_architecture(std::uint32_t hidden1, std::uint32_t hidden2) noexcept {
  return (hidden1 == kV1Hidden1 && hidden2 == kV1Hidden2) ||
         (hidden1 == kV2Hidden1 && hidden2 == kV2Hidden2);
}

bool validate_common(std::uint32_t feature_abi, std::uint32_t inputs,
                     std::uint32_t accumulator, std::uint32_t output,
                     std::uint32_t scalar, std::uint32_t endian,
                     std::uint64_t count, std::uint32_t hidden1,
                     std::uint32_t hidden2, std::string& error) {
  if (feature_abi != kNnueFeatureSetV1 || inputs != kNnueInputDimensions ||
      accumulator != kAccumulator || output != kOutput) {
    error = "NNUE incompatible feature/network dimensions";
    return false;
  }
  if (scalar != kScalarFloat32 || endian != kEndianMarker ||
      count != parameter_count(hidden1, hidden2) || !host_is_little_endian()) {
    error = "NNUE unsupported scalar type, endian marker, or parameter count";
    return false;
  }
  return true;
}

bool load_nnue_network_bytes_impl(const std::uint8_t* bytes, std::size_t size,
                                  std::string& error) {
  if (!bytes || size < 12) {
    error = "NNUE file is truncated (header)";
    return false;
  }

  if (std::memcmp(bytes, "HEBINNUE", 8) != 0) {
    error = "NNUE bad magic";
    return false;
  }
  std::uint32_t version{};
  std::memcpy(&version, bytes + 8, sizeof(version));

  std::uint32_t hidden1 = 0, hidden2 = 0;
  std::uint64_t count = 0, checksum = 0;
  float output_scale = 1.0F;
  FinalHiddenActivation final_hidden_activation = FinalHiddenActivation::ClippedRelu01;
  std::size_t header_size = 0;
  if (version == kV1Version) {
    header_size = sizeof(HeaderV1);
    if (size < header_size) { error = "NNUE file is truncated (header)"; return false; }
    HeaderV1 header{};
    std::memcpy(&header, bytes, sizeof(header));
    hidden1 = header.hidden1_dimensions; hidden2 = header.hidden2_dimensions;
    if (!validate_common(header.feature_abi, header.input_dimensions, header.accumulator_dimensions,
                         kOutput, header.scalar_type, header.endian_marker, header.parameter_count,
                         hidden1, hidden2, error)) return false;
    if (hidden1 != kV1Hidden1 || hidden2 != kV1Hidden2) {
      error = "NNUE incompatible feature/network dimensions"; return false;
    }
    count = header.parameter_count; checksum = header.checksum;
  } else if (version == kV2Version) {
    header_size = sizeof(HeaderV2);
    if (size < header_size) { error = "NNUE file is truncated (header)"; return false; }
    HeaderV2 header{};
    std::memcpy(&header, bytes, sizeof(header));
    hidden1 = header.hidden1_dimensions; hidden2 = header.hidden2_dimensions;
    if (!validate_common(header.feature_abi, header.input_dimensions, header.accumulator_dimensions,
                         header.output_dimensions, header.scalar_type, header.endian_marker,
                         header.parameter_count, hidden1, hidden2, error)) return false;
    if (hidden1 != kV2Hidden1 || hidden2 != kV2Hidden2) {
      error = "NNUE v2 supports only the frozen 128/128 architecture"; return false;
    }
    if (!std::isfinite(header.output_scale) || header.output_scale <= 0.0F) {
      error = "NNUE invalid output scale"; return false;
    }
    count = header.parameter_count; checksum = header.checksum; output_scale = header.output_scale;
  } else if (version == kV3Version) {
    header_size = sizeof(HeaderV3);
    if (size < header_size) { error = "NNUE file is truncated (header)"; return false; }
    HeaderV3 header{};
    std::memcpy(&header, bytes, sizeof(header));
    hidden1 = header.hidden1_dimensions; hidden2 = header.hidden2_dimensions;
    if (!validate_common(header.feature_abi, header.input_dimensions, header.accumulator_dimensions,
                         header.output_dimensions, header.scalar_type, header.endian_marker,
                         header.parameter_count, hidden1, hidden2, error)) return false;
    if (!supported_v3_architecture(hidden1, hidden2)) {
      error = "NNUE v3 supports only 32/32 or 128/128 architectures"; return false;
    }
    if (!supported_v3_activation(header.final_hidden_activation)) {
      error = "NNUE unsupported final hidden activation"; return false;
    }
    if (!std::isfinite(header.output_scale) || header.output_scale <= 0.0F) {
      error = "NNUE invalid output scale"; return false;
    }
    count = header.parameter_count; checksum = header.checksum; output_scale = header.output_scale;
    final_hidden_activation = static_cast<FinalHiddenActivation>(header.final_hidden_activation);
  } else {
    error = "NNUE unsupported format version";
    return false;
  }

  if (count > (std::numeric_limits<std::size_t>::max() / sizeof(float))) {
    error = "NNUE invalid parameter layout";
    return false;
  }
  const std::size_t payload_size = static_cast<std::size_t>(count) * sizeof(float);
  if (size < header_size + payload_size) {
    error = "NNUE file is truncated (parameters)";
    return false;
  }
  if (size != header_size + payload_size) {
    error = "NNUE file has trailing data";
    return false;
  }
  const std::uint8_t* payload = bytes + header_size;
  if (fnv1a_bytes(payload, payload_size) != checksum) {
    error = "NNUE checksum mismatch";
    return false;
  }

  Network network;
  network.hidden1_dimensions = hidden1;
  network.hidden2_dimensions = hidden2;
  network.output_scale = output_scale;
  network.final_hidden_activation = final_hidden_activation;
  std::size_t offset = 0;
  const auto take = [&](std::vector<float>& destination, std::size_t amount) {
    destination.resize(amount);
    std::memcpy(destination.data(), payload + offset, amount * sizeof(float));
    offset += amount * sizeof(float);
  };
  take(network.transform, kNnueInputDimensions * kAccumulator);
  take(network.transform_bias, kAccumulator);
  take(network.hidden1, static_cast<std::size_t>(hidden1) * 2 * kAccumulator);
  take(network.hidden1_bias, hidden1);
  take(network.hidden2, static_cast<std::size_t>(hidden2) * hidden1);
  take(network.hidden2_bias, hidden2);
  take(network.output, hidden2);
  std::memcpy(&network.output_bias, payload + offset, sizeof(network.output_bias));
  offset += sizeof(network.output_bias);
  if (offset != payload_size) { error = "NNUE invalid parameter layout"; return false; }
  loaded_network() = std::move(network);
  return true;
}

}  // namespace

bool load_nnue_network(const std::string& path, std::string& error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) { error = "cannot open NNUE file: " + path; return false; }
  file.seekg(0, std::ios::end);
  const std::streamoff length = file.tellg();
  if (length < 0 || static_cast<std::uint64_t>(length) > std::numeric_limits<std::size_t>::max()) {
    error = "NNUE file is too large";
    return false;
  }
  file.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
  if (!bytes.empty()) file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file && !bytes.empty()) { error = "cannot read NNUE file: " + path; return false; }
  return load_nnue_network_bytes_impl(bytes.data(), bytes.size(), error);
}

bool load_nnue_network_bytes(const std::uint8_t* bytes, std::size_t size, std::string& error) {
  return load_nnue_network_bytes_impl(bytes, size, error);
}

bool nnue_network_available() noexcept { return loaded_network().has_value(); }
void clear_nnue_network() noexcept { loaded_network().reset(); }

std::optional<float> evaluate_nnue_network_raw(const Board& board) noexcept {
  const auto& maybe = loaded_network(); if (!maybe) return std::nullopt;
  const Network& n = *maybe;
  std::vector<float> white = n.transform_bias, black = n.transform_bias;
  const auto add = [&](const NnueFeatures& features, std::vector<float>& accumulator) {
    for (std::size_t f = 0; f < features.size; ++f) {
      const float* row = n.transform.data() + static_cast<std::size_t>(features.indices[f]) * kAccumulator;
      for (std::size_t i = 0; i < kAccumulator; ++i) accumulator[i] += row[i];
    }
  };
  add(extract_nnue_features(board, Color::White), white);
  add(extract_nnue_features(board, Color::Black), black);
  const auto& stm = board.side_to_move() == Color::White ? white : black;
  const auto& opp = board.side_to_move() == Color::White ? black : white;
  float clipped_stm[kAccumulator];
  float clipped_opp[kAccumulator];
  for (std::size_t i = 0; i < kAccumulator; ++i) {
    clipped_stm[i] = clipped_relu(stm[i]);
    clipped_opp[i] = clipped_relu(opp[i]);
  }
  std::vector<float> h1(n.hidden1_dimensions);
  for (std::size_t o = 0; o < n.hidden1_dimensions; ++o) {
    float sum = n.hidden1_bias[o]; const float* row = n.hidden1.data() + o * 2 * kAccumulator;
    for (std::size_t i = 0; i < kAccumulator; ++i) sum += row[i] * clipped_stm[i];
    for (std::size_t i = 0; i < kAccumulator; ++i) sum += row[kAccumulator + i] * clipped_opp[i];
    h1[o] = clipped_relu(sum);
  }
  std::vector<float> h2(n.hidden2_dimensions);
  for (std::size_t o = 0; o < n.hidden2_dimensions; ++o) {
    float sum = n.hidden2_bias[o]; const float* row = n.hidden2.data() + o * n.hidden1_dimensions;
    for (std::size_t i = 0; i < n.hidden1_dimensions; ++i) sum += row[i] * h1[i];
    h2[o] = final_hidden_relu(sum, n.final_hidden_activation);
  }
  float score = n.output_bias;
  for (std::size_t i = 0; i < n.hidden2_dimensions; ++i) score += n.output[i] * h2[i];
  score *= n.output_scale;
  if (!std::isfinite(score)) return std::nullopt;
  return score;
}

#if defined(HEBICHESS_NNUE_TEST_REFERENCE)
std::optional<float> evaluate_nnue_network_raw_reference(const Board& board) noexcept {
  const auto& maybe = loaded_network(); if (!maybe) return std::nullopt;
  const Network& n = *maybe;
  std::vector<float> white = n.transform_bias, black = n.transform_bias;
  const auto add = [&](const NnueFeatures& features, std::vector<float>& accumulator) {
    for (std::size_t f = 0; f < features.size; ++f) {
      const float* row = n.transform.data() + static_cast<std::size_t>(features.indices[f]) * kAccumulator;
      for (std::size_t i = 0; i < kAccumulator; ++i) accumulator[i] += row[i];
    }
  };
  add(extract_nnue_features(board, Color::White), white);
  add(extract_nnue_features(board, Color::Black), black);
  const auto& stm = board.side_to_move() == Color::White ? white : black;
  const auto& opp = board.side_to_move() == Color::White ? black : white;
  std::vector<float> h1(n.hidden1_dimensions);
  for (std::size_t o = 0; o < n.hidden1_dimensions; ++o) {
    float sum = n.hidden1_bias[o]; const float* row = n.hidden1.data() + o * 2 * kAccumulator;
    for (std::size_t i = 0; i < kAccumulator; ++i) sum += row[i] * clipped_relu(stm[i]);
    for (std::size_t i = 0; i < kAccumulator; ++i) sum += row[kAccumulator + i] * clipped_relu(opp[i]);
    h1[o] = clipped_relu(sum);
  }
  std::vector<float> h2(n.hidden2_dimensions);
  for (std::size_t o = 0; o < n.hidden2_dimensions; ++o) {
    float sum = n.hidden2_bias[o]; const float* row = n.hidden2.data() + o * n.hidden1_dimensions;
    for (std::size_t i = 0; i < n.hidden1_dimensions; ++i) sum += row[i] * h1[i];
    h2[o] = final_hidden_relu(sum, n.final_hidden_activation);
  }
  float score = n.output_bias;
  for (std::size_t i = 0; i < n.hidden2_dimensions; ++i) score += n.output[i] * h2[i];
  score *= n.output_scale;
  if (!std::isfinite(score)) return std::nullopt;
  return score;
}
#endif

std::optional<int> evaluate_nnue_network(const Board& board) noexcept {
  const auto score = evaluate_nnue_network_raw(board);
  if (!score) return std::nullopt;
  return static_cast<int>(std::lround(*score));
}

}  // namespace hebichess
