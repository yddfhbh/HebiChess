#include "chess/nnue.hpp"

#include <algorithm>
#include <array>
#if defined(HEBICHESS_NNUE_DIAGNOSTICS) || defined(HEBICHESS_NNUE_TEST_PROFILE)
#include <chrono>
#endif
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
#if defined(HEBICHESS_NNUE_HIDDEN1_LAYOUT) && HEBICHESS_NNUE_HIDDEN1_LAYOUT != 0
  // Test-only transposition of hidden1 weights.  The serialized model stays
  // unchanged in `hidden1`; this cache is derived after strict parsing.
  std::vector<float> hidden1_prepacked;
#endif
};

std::optional<Network>& loaded_network() { static std::optional<Network> value; return value; }

// Original4 is the accepted production layout.  The packed forms are compiled
// only into dedicated diagnostics, never selected through UCI or a runtime
// option.
#ifndef HEBICHESS_NNUE_HIDDEN1_LAYOUT
#define HEBICHESS_NNUE_HIDDEN1_LAYOUT 0
#endif

static_assert(HEBICHESS_NNUE_HIDDEN1_LAYOUT == 0 ||
                  HEBICHESS_NNUE_HIDDEN1_LAYOUT == 4 ||
                  HEBICHESS_NNUE_HIDDEN1_LAYOUT == 8,
              "hidden1 layout must be Original4 (0), Prepacked4 (4), or Prepacked8 (8)");

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
#if HEBICHESS_NNUE_HIDDEN1_LAYOUT != 0
  constexpr std::size_t kPackedOutputs = HEBICHESS_NNUE_HIDDEN1_LAYOUT;
  const std::size_t groups = static_cast<std::size_t>(hidden1) / kPackedOutputs;
  network.hidden1_prepacked.resize(groups * 2 * kAccumulator * kPackedOutputs);
  for (std::size_t group = 0; group < groups; ++group) {
    for (std::size_t half = 0; half < 2; ++half) {
      for (std::size_t input = 0; input < kAccumulator; ++input) {
        float* destination = network.hidden1_prepacked.data() +
            ((group * 2 * kAccumulator + half * kAccumulator + input) * kPackedOutputs);
        for (std::size_t lane = 0; lane < kPackedOutputs; ++lane) {
          const std::size_t output = group * kPackedOutputs + lane;
          destination[lane] = network.hidden1[(output * 2 + half) * kAccumulator + input];
        }
      }
    }
  }
#endif
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

namespace {

// Every output preserves the historical floating-point addition order:
// stm[0..255], then opp[0..255].  Original4 merely exposes four independent
// sums.  Prepacked variants change only the weight-address layout.
template <std::size_t Outputs>
void hidden1_original4(const Network& network, const float* stm, const float* opp,
                       float* sums) noexcept {
  std::size_t output = 0;
  for (; output + Outputs <= network.hidden1_dimensions; output += Outputs) {
    std::array<float, Outputs> partial{};
    std::array<const float*, Outputs> rows{};
    for (std::size_t lane = 0; lane < Outputs; ++lane) {
      partial[lane] = network.hidden1_bias[output + lane];
      rows[lane] = network.hidden1.data() + (output + lane) * 2 * kAccumulator;
    }
    for (std::size_t input = 0; input < kAccumulator; ++input) {
      const float value = stm[input];
      for (std::size_t lane = 0; lane < Outputs; ++lane)
        partial[lane] += rows[lane][input] * value;
    }
    for (std::size_t input = 0; input < kAccumulator; ++input) {
      const float value = opp[input];
      for (std::size_t lane = 0; lane < Outputs; ++lane)
        partial[lane] += rows[lane][kAccumulator + input] * value;
    }
    for (std::size_t lane = 0; lane < Outputs; ++lane) sums[output + lane] = partial[lane];
  }
  // Current supported networks have hidden1 widths divisible by four, but
  // retain a scalar tail to keep this helper structurally total.
  for (; output < network.hidden1_dimensions; ++output) {
    float sum = network.hidden1_bias[output];
    const float* row = network.hidden1.data() + output * 2 * kAccumulator;
    for (std::size_t input = 0; input < kAccumulator; ++input) sum += row[input] * stm[input];
    for (std::size_t input = 0; input < kAccumulator; ++input)
      sum += row[kAccumulator + input] * opp[input];
    sums[output] = sum;
  }
}

#if HEBICHESS_NNUE_HIDDEN1_LAYOUT != 0
template <std::size_t Outputs>
void hidden1_prepacked(const Network& network, const float* stm, const float* opp,
                       float* sums) noexcept {
  const std::size_t groups = network.hidden1_dimensions / Outputs;
  for (std::size_t group = 0; group < groups; ++group) {
    std::array<float, Outputs> partial{};
    const std::size_t output = group * Outputs;
    for (std::size_t lane = 0; lane < Outputs; ++lane)
      partial[lane] = network.hidden1_bias[output + lane];
    const float* packed = network.hidden1_prepacked.data() +
        group * 2 * kAccumulator * Outputs;
    for (std::size_t input = 0; input < kAccumulator; ++input) {
      const float* weights = packed + input * Outputs;
      for (std::size_t lane = 0; lane < Outputs; ++lane) partial[lane] += weights[lane] * stm[input];
    }
    packed += kAccumulator * Outputs;
    for (std::size_t input = 0; input < kAccumulator; ++input) {
      const float* weights = packed + input * Outputs;
      for (std::size_t lane = 0; lane < Outputs; ++lane) partial[lane] += weights[lane] * opp[input];
    }
    for (std::size_t lane = 0; lane < Outputs; ++lane) sums[output + lane] = partial[lane];
  }
}
#endif

void hidden1_active(const Network& network, const float* stm, const float* opp,
                    float* sums) noexcept {
#if HEBICHESS_NNUE_HIDDEN1_LAYOUT == 0
  hidden1_original4<4>(network, stm, opp, sums);
#elif HEBICHESS_NNUE_HIDDEN1_LAYOUT == 4
  hidden1_prepacked<4>(network, stm, opp, sums);
#else
  hidden1_prepacked<8>(network, stm, opp, sums);
#endif
}

float finish_from_hidden1(const Network& network, std::vector<float>& hidden1) noexcept {
  for (float& value : hidden1) value = clipped_relu(value);
  std::vector<float> hidden2(network.hidden2_dimensions);
  for (std::size_t output = 0; output < network.hidden2_dimensions; ++output) {
    float sum = network.hidden2_bias[output];
    const float* row = network.hidden2.data() + output * network.hidden1_dimensions;
    for (std::size_t input = 0; input < network.hidden1_dimensions; ++input)
      sum += row[input] * hidden1[input];
    hidden2[output] = final_hidden_relu(sum, network.final_hidden_activation);
  }
  float score = network.output_bias;
  for (std::size_t input = 0; input < network.hidden2_dimensions; ++input)
    score += network.output[input] * hidden2[input];
  return score * network.output_scale;
}

using AccumulatorArray = std::array<float, kAccumulator>;

void add_features_to_accumulator(const Network& network, const NnueFeatures& features,
                                 AccumulatorArray& accumulator) noexcept {
  for (std::size_t feature = 0; feature < features.size; ++feature) {
    const float* row = network.transform.data() +
        static_cast<std::size_t>(features.indices[feature]) * kAccumulator;
    for (std::size_t input = 0; input < kAccumulator; ++input) accumulator[input] += row[input];
  }
}

void refresh_perspective_accumulator(const Network& network, const Board& board,
                                     Color perspective, AccumulatorArray& accumulator) noexcept {
  std::copy_n(network.transform_bias.begin(), kAccumulator, accumulator.begin());
  add_features_to_accumulator(network, extract_nnue_features(board, perspective), accumulator);
}

void add_feature_delta(const Network& network, AccumulatorArray& accumulator,
                       Square perspective_king, Piece piece, Square square,
                       Color perspective, float sign) noexcept {
  if (piece.is_empty()) return;
  const std::uint32_t feature = nnue_feature_index(perspective_king, piece, square, perspective);
  const float* row = network.transform.data() + static_cast<std::size_t>(feature) * kAccumulator;
  for (std::size_t input = 0; input < kAccumulator; ++input) accumulator[input] += sign * row[input];
}

std::optional<float> evaluate_from_accumulator(const Network& network, const Board& board,
                                               const NnueAccumulator& accumulator) noexcept {
  const auto& stm = board.side_to_move() == Color::White ? accumulator.white : accumulator.black;
  const auto& opp = board.side_to_move() == Color::White ? accumulator.black : accumulator.white;
  std::array<float, kAccumulator> clipped_stm, clipped_opp;
  for (std::size_t input = 0; input < kAccumulator; ++input) {
    clipped_stm[input] = clipped_relu(stm[input]);
    clipped_opp[input] = clipped_relu(opp[input]);
  }
  std::vector<float> h1(network.hidden1_dimensions);
  hidden1_active(network, clipped_stm.data(), clipped_opp.data(), h1.data());
  const float score = finish_from_hidden1(network, h1);
  return std::isfinite(score) ? std::optional<float>(score) : std::nullopt;
}

}  // namespace

bool refresh_nnue_accumulator(const Board& board, NnueAccumulator& accumulator) noexcept {
  const auto& maybe = loaded_network();
  if (!maybe) return false;
  refresh_perspective_accumulator(*maybe, board, Color::White, accumulator.white);
  refresh_perspective_accumulator(*maybe, board, Color::Black, accumulator.black);
  return true;
}

bool update_nnue_accumulator(const Board& parent, const Move& move,
                             const NnueAccumulator& parent_accumulator,
                             NnueAccumulator& child_accumulator) noexcept {
  const auto& maybe = loaded_network();
  if (!maybe) return false;
  const Network& network = *maybe;
  const Piece moving = parent.piece_at(move.from);
  if (moving.is_empty()) return false;

  Piece captured = parent.piece_at(move.to);
  Square captured_square = move.to;
  if (move.flag == MoveFlag::EnPassant) {
    captured_square = Square::from_file_rank(move.to.file(), move.from.rank());
    captured = parent.piece_at(captured_square);
  }
  Piece placed = moving;
  if (move.is_promotion()) placed.type = move.promotion;

  child_accumulator = parent_accumulator;
  std::optional<Board> child_board;
  const auto refresh_child_perspective = [&](Color perspective,
                                             AccumulatorArray& accumulator) {
    if (!child_board.has_value()) {
      child_board = parent;
      child_board->make_move(move);
    }
    refresh_perspective_accumulator(network, *child_board, perspective, accumulator);
  };
  const auto update_perspective = [&](Color perspective,
                                      AccumulatorArray& accumulator) -> bool {
    if (moving.type == PieceType::King && moving.color == perspective) {
      refresh_child_perspective(perspective, accumulator);
      return true;
    }
    const Square perspective_king = parent.find_king(perspective);
    if (!perspective_king.is_valid()) return false;
    add_feature_delta(network, accumulator, perspective_king, moving, move.from,
                      perspective, -1.0F);
    add_feature_delta(network, accumulator, perspective_king, captured, captured_square,
                      perspective, -1.0F);
    add_feature_delta(network, accumulator, perspective_king, placed, move.to,
                      perspective, 1.0F);
    if (move.flag == MoveFlag::CastleKingSide || move.flag == MoveFlag::CastleQueenSide) {
      const std::uint8_t rook_from_file = move.flag == MoveFlag::CastleKingSide ? 7 : 0;
      const std::uint8_t rook_to_file = move.flag == MoveFlag::CastleKingSide ? 5 : 3;
      const Square rook_from = Square::from_file_rank(rook_from_file, move.from.rank());
      const Square rook_to = Square::from_file_rank(rook_to_file, move.from.rank());
      const Piece rook = parent.piece_at(rook_from);
      add_feature_delta(network, accumulator, perspective_king, rook, rook_from,
                        perspective, -1.0F);
      add_feature_delta(network, accumulator, perspective_king, rook, rook_to,
                        perspective, 1.0F);
    }
    return true;
  };
  return update_perspective(Color::White, child_accumulator.white) &&
         update_perspective(Color::Black, child_accumulator.black);
}

std::optional<float> evaluate_nnue_network_raw_from_accumulator(
    const Board& board, const NnueAccumulator& accumulator) noexcept {
  const auto& maybe = loaded_network();
  if (!maybe) return std::nullopt;
  return evaluate_from_accumulator(*maybe, board, accumulator);
}

std::optional<float> evaluate_nnue_network_raw(const Board& board) noexcept {
  NnueAccumulator accumulator;
  if (!refresh_nnue_accumulator(board, accumulator)) return std::nullopt;
  return evaluate_nnue_network_raw_from_accumulator(board, accumulator);
}

#if defined(HEBICHESS_NNUE_TEST_PROFILE)
std::optional<NnueEvaluatorStageProfile> profile_nnue_evaluator_stages(
    const std::vector<Board>& boards, std::size_t repeats) noexcept {
  const auto& maybe = loaded_network();
  if (!maybe || boards.empty() || repeats == 0) return std::nullopt;
  const Network& network = *maybe;
  std::vector<NnueAccumulator> accumulators(boards.size());
  std::vector<std::array<float, kAccumulator>> clipped_stm(boards.size()), clipped_opp(boards.size());
  std::vector<std::vector<float>> hidden1(boards.size(), std::vector<float>(network.hidden1_dimensions));
  volatile float sink = 0.0F;
  const auto per_eval_us = [&](const auto& started) {
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count() /
        static_cast<double>(boards.size() * repeats);
  };

  const auto rebuild_started = std::chrono::steady_clock::now();
  for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
    for (std::size_t index = 0; index < boards.size(); ++index) {
      refresh_perspective_accumulator(network, boards[index], Color::White, accumulators[index].white);
      refresh_perspective_accumulator(network, boards[index], Color::Black, accumulators[index].black);
      sink += accumulators[index].white[0];
    }
  }
  const double rebuild_us = per_eval_us(rebuild_started);

  const auto clip_started = std::chrono::steady_clock::now();
  for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
    for (std::size_t index = 0; index < boards.size(); ++index) {
      const auto& stm = boards[index].side_to_move() == Color::White ? accumulators[index].white : accumulators[index].black;
      const auto& opp = boards[index].side_to_move() == Color::White ? accumulators[index].black : accumulators[index].white;
      for (std::size_t input = 0; input < kAccumulator; ++input) {
        clipped_stm[index][input] = clipped_relu(stm[input]);
        clipped_opp[index][input] = clipped_relu(opp[input]);
      }
      sink += clipped_stm[index][0];
    }
  }
  const double clip_us = per_eval_us(clip_started);

  const auto hidden1_started = std::chrono::steady_clock::now();
  for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
    for (std::size_t index = 0; index < boards.size(); ++index) {
      hidden1_active(network, clipped_stm[index].data(), clipped_opp[index].data(), hidden1[index].data());
      sink += hidden1[index][0];
    }
  }
  const double hidden1_us = per_eval_us(hidden1_started);

  const auto finish_started = std::chrono::steady_clock::now();
  for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
    for (std::size_t index = 0; index < boards.size(); ++index) sink += finish_from_hidden1(network, hidden1[index]);
  }
  const double finish_us = per_eval_us(finish_started);
  (void)sink;
  return NnueEvaluatorStageProfile{rebuild_us, clip_us, hidden1_us, finish_us, 0.0};
}
#endif

#if defined(HEBICHESS_NNUE_TEST_REFERENCE)
std::optional<float> evaluate_nnue_network_raw_reference(const Board& board) noexcept {
  const auto& maybe = loaded_network();
  if (!maybe) return std::nullopt;
  const Network& network = *maybe;
  std::vector<float> white = network.transform_bias, black = network.transform_bias;
  const auto add = [&](const NnueFeatures& features, std::vector<float>& accumulator) {
    for (std::size_t feature = 0; feature < features.size; ++feature) {
      const float* row = network.transform.data() + static_cast<std::size_t>(features.indices[feature]) * kAccumulator;
      for (std::size_t input = 0; input < kAccumulator; ++input) accumulator[input] += row[input];
    }
  };
  add(extract_nnue_features(board, Color::White), white);
  add(extract_nnue_features(board, Color::Black), black);
  const auto& stm = board.side_to_move() == Color::White ? white : black;
  const auto& opp = board.side_to_move() == Color::White ? black : white;
  std::vector<float> h1(network.hidden1_dimensions);
  for (std::size_t output = 0; output < network.hidden1_dimensions; ++output) {
    float sum = network.hidden1_bias[output];
    const float* row = network.hidden1.data() + output * 2 * kAccumulator;
    for (std::size_t input = 0; input < kAccumulator; ++input) sum += row[input] * clipped_relu(stm[input]);
    for (std::size_t input = 0; input < kAccumulator; ++input)
      sum += row[kAccumulator + input] * clipped_relu(opp[input]);
    h1[output] = clipped_relu(sum);
  }
  std::vector<float> h2(network.hidden2_dimensions);
  for (std::size_t output = 0; output < network.hidden2_dimensions; ++output) {
    float sum = network.hidden2_bias[output];
    const float* row = network.hidden2.data() + output * network.hidden1_dimensions;
    for (std::size_t input = 0; input < network.hidden1_dimensions; ++input) sum += row[input] * h1[input];
    h2[output] = final_hidden_relu(sum, network.final_hidden_activation);
  }
  float score = network.output_bias;
  for (std::size_t input = 0; input < network.hidden2_dimensions; ++input) score += network.output[input] * h2[input];
  score *= network.output_scale;
  return std::isfinite(score) ? std::optional<float>(score) : std::nullopt;
}
#endif

#if defined(HEBICHESS_NNUE_VARIANT_TEST)
std::optional<NnueVariantParity> nnue_variant_parity_for_test(const Board& board) noexcept {
  const auto& maybe = loaded_network();
  if (!maybe) return std::nullopt;
  const Network& network = *maybe;
  NnueAccumulator accumulator;
  if (!refresh_nnue_accumulator(board, accumulator)) return std::nullopt;
  const auto& stm = board.side_to_move() == Color::White ? accumulator.white : accumulator.black;
  const auto& opp = board.side_to_move() == Color::White ? accumulator.black : accumulator.white;
  std::array<float, kAccumulator> clipped_stm, clipped_opp;
  for (std::size_t input = 0; input < kAccumulator; ++input) {
    clipped_stm[input] = clipped_relu(stm[input]);
    clipped_opp[input] = clipped_relu(opp[input]);
  }
  std::vector<float> active(network.hidden1_dimensions), original4(network.hidden1_dimensions);
  hidden1_active(network, clipped_stm.data(), clipped_opp.data(), active.data());
  hidden1_original4<4>(network, clipped_stm.data(), clipped_opp.data(), original4.data());
  NnueVariantParity parity;
  for (std::size_t output = 0; output < active.size(); ++output)
    parity.hidden1_max_abs_diff = std::max(parity.hidden1_max_abs_diff,
                                           std::abs(active[output] - original4[output]));
  const float active_raw = finish_from_hidden1(network, active);
  const float original4_raw = finish_from_hidden1(network, original4);
  parity.raw_max_abs_diff = std::abs(active_raw - original4_raw);
  parity.active_cp = static_cast<int>(std::lround(active_raw));
  parity.original4_cp = static_cast<int>(std::lround(original4_raw));
  return parity;
}
#endif

std::optional<int> evaluate_nnue_network(const Board& board) noexcept {
  const auto score = evaluate_nnue_network_raw(board);
  if (!score) return std::nullopt;
  return static_cast<int>(std::lround(*score));
}

#ifdef HEBICHESS_NNUE_DIAGNOSTICS
namespace {

constexpr std::size_t kDiagnosticMaxHidden = kV2Hidden1;

template <class Accumulator>
void diagnostic_add_features(const Network& network, const NnueFeatures& features,
                             Accumulator& accumulator) noexcept {
  for (std::size_t f = 0; f < features.size; ++f) {
    const float* row = network.transform.data() +
        static_cast<std::size_t>(features.indices[f]) * kAccumulator;
    for (std::size_t i = 0; i < kAccumulator; ++i) accumulator[i] += row[i];
  }
}

float diagnostic_finish_score(const Network& network, const float* h2) noexcept {
  float score = network.output_bias;
  for (std::size_t i = 0; i < network.hidden2_dimensions; ++i)
    score += network.output[i] * h2[i];
  return score * network.output_scale;
}

float diagnostic_dense_sum_repeated_clip(const float* row, const float* stm,
                                          const float* opp, float bias) noexcept {
  // This is deliberately a two-span loop.  It is algebraically identical to
  // Legacy, retains its addition order, but makes the dense spans explicit.
  float sum = bias;
  const float* stm_row = row;
  const float* opp_row = row + kAccumulator;
  for (std::size_t i = 0; i < kAccumulator; ++i)
    sum += stm_row[i] * clipped_relu(stm[i]);
  for (std::size_t i = 0; i < kAccumulator; ++i)
    sum += opp_row[i] * clipped_relu(opp[i]);
  return sum;
}

float diagnostic_dense_sum_clipped(const float* row, const float* stm,
                                   const float* opp, float bias) noexcept {
  float sum = bias;
  const float* stm_row = row;
  const float* opp_row = row + kAccumulator;
  for (std::size_t i = 0; i < kAccumulator; ++i) sum += stm_row[i] * stm[i];
  for (std::size_t i = 0; i < kAccumulator; ++i) sum += opp_row[i] * opp[i];
  return sum;
}

template <class H1>
void diagnostic_hidden2(const Network& network, const H1& h1, float* h2) noexcept {
  for (std::size_t o = 0; o < network.hidden2_dimensions; ++o) {
    float sum = network.hidden2_bias[o];
    const float* row = network.hidden2.data() + o * network.hidden1_dimensions;
    for (std::size_t i = 0; i < network.hidden1_dimensions; ++i) sum += row[i] * h1[i];
    h2[o] = final_hidden_relu(sum, network.final_hidden_activation);
  }
}

std::optional<float> diagnostic_legacy(const Board& board, const Network& network) noexcept {
  // Matches the pre-2A dynamic evaluator structure: four per-evaluation
  // vectors and clipped_relu performed in every hidden-1 dot product.
  std::vector<float> white = network.transform_bias, black = network.transform_bias;
  diagnostic_add_features(network, extract_nnue_features(board, Color::White), white);
  diagnostic_add_features(network, extract_nnue_features(board, Color::Black), black);
  const auto& stm = board.side_to_move() == Color::White ? white : black;
  const auto& opp = board.side_to_move() == Color::White ? black : white;
  std::vector<float> h1(network.hidden1_dimensions);
  for (std::size_t o = 0; o < network.hidden1_dimensions; ++o) {
    const float* row = network.hidden1.data() + o * 2 * kAccumulator;
    float sum = network.hidden1_bias[o];
    for (std::size_t i = 0; i < kAccumulator; ++i) sum += row[i] * clipped_relu(stm[i]);
    for (std::size_t i = 0; i < kAccumulator; ++i)
      sum += row[kAccumulator + i] * clipped_relu(opp[i]);
    h1[o] = clipped_relu(sum);
  }
  std::vector<float> h2(network.hidden2_dimensions);
  diagnostic_hidden2(network, h1, h2.data());
  const float score = diagnostic_finish_score(network, h2.data());
  return std::isfinite(score) ? std::optional<float>(score) : std::nullopt;
}

std::optional<float> diagnostic_allocation_only(const Board& board,
                                                 const Network& network) noexcept {
  // No braces: every scratch element is copied or overwritten before read.
  std::array<float, kAccumulator> white;
  std::array<float, kAccumulator> black;
  std::copy_n(network.transform_bias.data(), kAccumulator, white.data());
  std::copy_n(network.transform_bias.data(), kAccumulator, black.data());
  diagnostic_add_features(network, extract_nnue_features(board, Color::White), white);
  diagnostic_add_features(network, extract_nnue_features(board, Color::Black), black);
  const float* stm = board.side_to_move() == Color::White ? white.data() : black.data();
  const float* opp = board.side_to_move() == Color::White ? black.data() : white.data();
  std::array<float, kDiagnosticMaxHidden> h1;
  for (std::size_t o = 0; o < network.hidden1_dimensions; ++o) {
    const float* row = network.hidden1.data() + o * 2 * kAccumulator;
    float sum = network.hidden1_bias[o];
    for (std::size_t i = 0; i < kAccumulator; ++i) sum += row[i] * clipped_relu(stm[i]);
    for (std::size_t i = 0; i < kAccumulator; ++i)
      sum += row[kAccumulator + i] * clipped_relu(opp[i]);
    h1[o] = clipped_relu(sum);
  }
  std::array<float, kDiagnosticMaxHidden> h2;
  diagnostic_hidden2(network, h1, h2.data());
  const float score = diagnostic_finish_score(network, h2.data());
  return std::isfinite(score) ? std::optional<float>(score) : std::nullopt;
}

std::optional<float> diagnostic_clip_precompute_only(const Board& board,
                                                      const Network& network) noexcept {
  // Keep Legacy's allocations; only move the two accumulator clip passes out
  // of the hidden-1 loop.
  std::vector<float> white = network.transform_bias, black = network.transform_bias;
  diagnostic_add_features(network, extract_nnue_features(board, Color::White), white);
  diagnostic_add_features(network, extract_nnue_features(board, Color::Black), black);
  const auto& stm = board.side_to_move() == Color::White ? white : black;
  const auto& opp = board.side_to_move() == Color::White ? black : white;
  std::array<float, kAccumulator> clipped_stm;
  std::array<float, kAccumulator> clipped_opp;
  for (std::size_t i = 0; i < kAccumulator; ++i) {
    clipped_stm[i] = clipped_relu(stm[i]);
    clipped_opp[i] = clipped_relu(opp[i]);
  }
  std::vector<float> h1(network.hidden1_dimensions);
  for (std::size_t o = 0; o < network.hidden1_dimensions; ++o) {
    float sum = network.hidden1_bias[o];
    const float* row = network.hidden1.data() + o * 2 * kAccumulator;
    for (std::size_t i = 0; i < kAccumulator; ++i) sum += row[i] * clipped_stm[i];
    for (std::size_t i = 0; i < kAccumulator; ++i) sum += row[kAccumulator + i] * clipped_opp[i];
    h1[o] = clipped_relu(sum);
  }
  std::vector<float> h2(network.hidden2_dimensions);
  diagnostic_hidden2(network, h1, h2.data());
  const float score = diagnostic_finish_score(network, h2.data());
  return std::isfinite(score) ? std::optional<float>(score) : std::nullopt;
}

std::optional<float> diagnostic_dense_loop_only(const Board& board,
                                                 const Network& network) noexcept {
  // Keep Legacy's allocations and repeated clips.  Only the hidden-1 dense
  // loop is expressed through the cleaned, explicit two-span helper.
  std::vector<float> white = network.transform_bias, black = network.transform_bias;
  diagnostic_add_features(network, extract_nnue_features(board, Color::White), white);
  diagnostic_add_features(network, extract_nnue_features(board, Color::Black), black);
  const auto& stm = board.side_to_move() == Color::White ? white : black;
  const auto& opp = board.side_to_move() == Color::White ? black : white;
  std::vector<float> h1(network.hidden1_dimensions);
  for (std::size_t o = 0; o < network.hidden1_dimensions; ++o) {
    const float* row = network.hidden1.data() + o * 2 * kAccumulator;
    h1[o] = clipped_relu(diagnostic_dense_sum_repeated_clip(row, stm.data(), opp.data(),
                                                              network.hidden1_bias[o]));
  }
  std::vector<float> h2(network.hidden2_dimensions);
  diagnostic_hidden2(network, h1, h2.data());
  const float score = diagnostic_finish_score(network, h2.data());
  return std::isfinite(score) ? std::optional<float>(score) : std::nullopt;
}

std::optional<float> diagnostic_combined_current(const Board& board,
                                                  const Network& network) noexcept {
  std::array<float, kAccumulator> white;
  std::array<float, kAccumulator> black;
  std::copy_n(network.transform_bias.data(), kAccumulator, white.data());
  std::copy_n(network.transform_bias.data(), kAccumulator, black.data());
  diagnostic_add_features(network, extract_nnue_features(board, Color::White), white);
  diagnostic_add_features(network, extract_nnue_features(board, Color::Black), black);
  const float* stm = board.side_to_move() == Color::White ? white.data() : black.data();
  const float* opp = board.side_to_move() == Color::White ? black.data() : white.data();
  std::array<float, kAccumulator> clipped_stm;
  std::array<float, kAccumulator> clipped_opp;
  for (std::size_t i = 0; i < kAccumulator; ++i) {
    clipped_stm[i] = clipped_relu(stm[i]);
    clipped_opp[i] = clipped_relu(opp[i]);
  }
  std::array<float, kDiagnosticMaxHidden> h1;
  for (std::size_t o = 0; o < network.hidden1_dimensions; ++o) {
    float sum = network.hidden1_bias[o];
    const float* row = network.hidden1.data() + o * 2 * kAccumulator;
    h1[o] = clipped_relu(diagnostic_dense_sum_clipped(row, clipped_stm.data(),
                                                        clipped_opp.data(), sum));
  }
  std::array<float, kDiagnosticMaxHidden> h2;
  diagnostic_hidden2(network, h1, h2.data());
  const float score = diagnostic_finish_score(network, h2.data());
  return std::isfinite(score) ? std::optional<float>(score) : std::nullopt;
}

}  // namespace

NnueDiagnosticScratchArrays nnue_diagnostic_scratch_arrays() noexcept {
  return {kAccumulator, sizeof(std::array<float, kAccumulator>),
          kAccumulator, sizeof(std::array<float, kAccumulator>),
          kDiagnosticMaxHidden, sizeof(std::array<float, kDiagnosticMaxHidden>),
          kDiagnosticMaxHidden, sizeof(std::array<float, kDiagnosticMaxHidden>),
          kAccumulator, sizeof(std::array<float, kAccumulator>),
          kAccumulator, sizeof(std::array<float, kAccumulator>)};
}

std::optional<float> evaluate_nnue_network_raw_diagnostic(
    const Board& board, NnueDiagnosticVariant variant) noexcept {
  const auto& maybe = loaded_network();
  if (!maybe) return std::nullopt;
  switch (variant) {
    case NnueDiagnosticVariant::Legacy: return diagnostic_legacy(board, *maybe);
    case NnueDiagnosticVariant::AllocationOnly: return diagnostic_allocation_only(board, *maybe);
    case NnueDiagnosticVariant::ClipPrecomputeOnly: return diagnostic_clip_precompute_only(board, *maybe);
    case NnueDiagnosticVariant::DenseLoopOnly: return diagnostic_dense_loop_only(board, *maybe);
    case NnueDiagnosticVariant::CombinedCurrent: return diagnostic_combined_current(board, *maybe);
  }
  return std::nullopt;
}

NnueDiagnosticAllocationTiming nnue_diagnostic_measure_allocations(
    std::size_t iterations) noexcept {
  const auto& maybe = loaded_network();
  if (!maybe || iterations == 0) return {0.0, 0.0};
  const Network& network = *maybe;
  volatile std::size_t sink = 0;
  const auto reserve_started = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < iterations; ++i) {
    std::vector<float> white; white.reserve(kAccumulator);
    std::vector<float> black; black.reserve(kAccumulator);
    std::vector<float> h1; h1.reserve(network.hidden1_dimensions);
    std::vector<float> h2; h2.reserve(network.hidden2_dimensions);
    sink = sink + white.capacity() + black.capacity() + h1.capacity() + h2.capacity();
  }
  const auto reserve_elapsed = std::chrono::steady_clock::now() - reserve_started;
  const auto setup_started = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < iterations; ++i) {
    std::vector<float> white = network.transform_bias;
    std::vector<float> black = network.transform_bias;
    std::vector<float> h1(network.hidden1_dimensions);
    std::vector<float> h2(network.hidden2_dimensions);
    sink = sink + static_cast<std::size_t>(white[0] + black[0] + h1[0] + h2[0]);
  }
  const auto setup_elapsed = std::chrono::steady_clock::now() - setup_started;
  (void)sink;
  const double divisor = static_cast<double>(iterations);
  return {std::chrono::duration<double, std::micro>(reserve_elapsed).count() / divisor,
          std::chrono::duration<double, std::micro>(setup_elapsed).count() / divisor};
}
#endif

}  // namespace hebichess
