#include "chess/nnue.hpp"

#include <algorithm>
#include <array>
#if defined(HEBICHESS_NNUE_TEST_PROFILE)
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
constexpr std::uint32_t kAccumulator = kNnueAccumulatorDimensions;
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

// The normal engine build uses the accepted four-output dense loop.
// Benchmark-only targets override this at compile time; there is no runtime
// switch in the engine, UCI, or WASM surface.
#ifndef HEBICHESS_NNUE_HIDDEN1_VARIANT
#define HEBICHESS_NNUE_HIDDEN1_VARIANT 4
#endif

static_assert(HEBICHESS_NNUE_HIDDEN1_VARIANT == 1 ||
                  HEBICHESS_NNUE_HIDDEN1_VARIANT == 2 ||
                  HEBICHESS_NNUE_HIDDEN1_VARIANT == 4 ||
                  HEBICHESS_NNUE_HIDDEN1_VARIANT == 8,
              "hidden1 dense variant must be legacy, 2, 4, or 8 outputs");

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

using AccumulatorArray = std::array<float, kAccumulator>;

// Keep every neuron's floating-point accumulation order identical to the
// original implementation: first stm[0..255], then opp[0..255].  The
// interleaved forms only expose independent output accumulators to the
// compiler, allowing it to reuse each clipped input load.
#if defined(__GNUC__) && !defined(__clang__)
#define HEBICHESS_NNUE_NO_FP_CONTRACT __attribute__((optimize("fp-contract=off")))
#else
#define HEBICHESS_NNUE_NO_FP_CONTRACT
#endif
void hidden1_dense_legacy(const Network& network, const float* stm,
                          const float* opp, float* sums,
                          std::size_t first_output = 0) noexcept {
  for (std::size_t o = first_output; o < network.hidden1_dimensions; ++o) {
    float sum = network.hidden1_bias[o];
    const float* row = network.hidden1.data() + o * 2 * kAccumulator;
    for (std::size_t i = 0; i < kAccumulator; ++i) sum += row[i] * stm[i];
    for (std::size_t i = 0; i < kAccumulator; ++i)
      sum += row[kAccumulator + i] * opp[i];
    sums[o] = sum;
  }
}

template <std::size_t Outputs>
HEBICHESS_NNUE_NO_FP_CONTRACT
void hidden1_dense_interleaved(const Network& network, const float* stm,
                               const float* opp, float* sums) noexcept {
  std::size_t o = 0;
  for (; o + Outputs <= network.hidden1_dimensions; o += Outputs) {
    std::array<float, Outputs> partial{};
    std::array<const float*, Outputs> rows{};
    for (std::size_t lane = 0; lane < Outputs; ++lane) {
      partial[lane] = network.hidden1_bias[o + lane];
      rows[lane] = network.hidden1.data() + (o + lane) * 2 * kAccumulator;
    }
    for (std::size_t i = 0; i < kAccumulator; ++i) {
      const float input = stm[i];
      for (std::size_t lane = 0; lane < Outputs; ++lane)
        partial[lane] += rows[lane][i] * input;
    }
    for (std::size_t i = 0; i < kAccumulator; ++i) {
      const float input = opp[i];
      for (std::size_t lane = 0; lane < Outputs; ++lane)
        partial[lane] += rows[lane][kAccumulator + i] * input;
    }
    for (std::size_t lane = 0; lane < Outputs; ++lane) sums[o + lane] = partial[lane];
  }
  hidden1_dense_legacy(network, stm, opp, sums, o);
}

void hidden1_dense(const Network& network, const float* stm, const float* opp,
                   float* sums) noexcept {
#if HEBICHESS_NNUE_HIDDEN1_VARIANT == 1
  hidden1_dense_legacy(network, stm, opp, sums);
#elif HEBICHESS_NNUE_HIDDEN1_VARIANT == 2
  hidden1_dense_interleaved<2>(network, stm, opp, sums);
#elif HEBICHESS_NNUE_HIDDEN1_VARIANT == 4
  hidden1_dense_interleaved<4>(network, stm, opp, sums);
#else
  hidden1_dense_interleaved<8>(network, stm, opp, sums);
#endif
}
#undef HEBICHESS_NNUE_NO_FP_CONTRACT

void add_features_to_accumulator(const Network& network,
                                 const NnueFeatures& features,
                                 AccumulatorArray& accumulator) noexcept {
  for (std::size_t f = 0; f < features.size; ++f) {
    const float* row = network.transform.data() +
        static_cast<std::size_t>(features.indices[f]) * kAccumulator;
    for (std::size_t i = 0; i < kAccumulator; ++i) accumulator[i] += row[i];
  }
}

void refresh_perspective_accumulator(const Network& network, const Board& board,
                                     Color perspective,
                                     AccumulatorArray& accumulator) noexcept {
  std::copy_n(network.transform_bias.begin(), kAccumulator, accumulator.begin());
  add_features_to_accumulator(network, extract_nnue_features(board, perspective),
                              accumulator);
}

void add_feature_delta(const Network& network, AccumulatorArray& accumulator,
                       Square perspective_king, Piece piece, Square square,
                       Color perspective, float sign) noexcept {
  if (piece.is_empty()) return;
  const std::uint32_t feature = nnue_feature_index(perspective_king, piece,
                                                   square, perspective);
  const float* row = network.transform.data() +
      static_cast<std::size_t>(feature) * kAccumulator;
  for (std::size_t i = 0; i < kAccumulator; ++i) accumulator[i] += sign * row[i];
}

std::optional<float> evaluate_from_accumulator(const Network& network,
                                               const Board& board,
                                               const NnueAccumulator& accumulator) noexcept {
  const auto& stm = board.side_to_move() == Color::White ? accumulator.white
                                                          : accumulator.black;
  const auto& opp = board.side_to_move() == Color::White ? accumulator.black
                                                          : accumulator.white;
  float clipped_stm[kAccumulator];
  float clipped_opp[kAccumulator];
  for (std::size_t i = 0; i < kAccumulator; ++i) {
    clipped_stm[i] = clipped_relu(stm[i]);
    clipped_opp[i] = clipped_relu(opp[i]);
  }
  std::vector<float> h1(network.hidden1_dimensions);
  hidden1_dense(network, clipped_stm, clipped_opp, h1.data());
  for (float& value : h1) value = clipped_relu(value);
  std::vector<float> h2(network.hidden2_dimensions);
  for (std::size_t o = 0; o < network.hidden2_dimensions; ++o) {
    float sum = network.hidden2_bias[o];
    const float* row = network.hidden2.data() + o * network.hidden1_dimensions;
    for (std::size_t i = 0; i < network.hidden1_dimensions; ++i) sum += row[i] * h1[i];
    h2[o] = final_hidden_relu(sum, network.final_hidden_activation);
  }
  float score = network.output_bias;
  for (std::size_t i = 0; i < network.hidden2_dimensions; ++i) score += network.output[i] * h2[i];
  score *= network.output_scale;
  if (!std::isfinite(score)) return std::nullopt;
  return score;
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

bool refresh_nnue_accumulator(const Board& board, NnueAccumulator& accumulator) noexcept {
  const auto& maybe = loaded_network(); if (!maybe) return false;
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
namespace {

struct NnueEvaluatorStageScratch {
  NnueAccumulator accumulator{};
  AccumulatorArray clipped_white{};
  AccumulatorArray clipped_black{};
  std::array<float, kV2Hidden1> hidden1_sums{};
  std::array<float, kV2Hidden1> hidden1{};
  std::array<float, kV2Hidden2> hidden2{};
};

double profile_us_per_evaluation(const std::chrono::steady_clock::duration& elapsed,
                                 std::size_t boards, std::size_t repeats) noexcept {
  return std::chrono::duration<double, std::micro>(elapsed).count() /
      static_cast<double>(boards * repeats);
}

}  // namespace

std::optional<NnueEvaluatorStageProfile> profile_nnue_evaluator_stages(
    const std::vector<Board>& boards, std::size_t repeats) noexcept {
  const auto& maybe = loaded_network();
  if (!maybe || boards.empty() || repeats == 0) return std::nullopt;
  const Network& network = *maybe;
  std::vector<NnueEvaluatorStageScratch> scratch(boards.size());
  volatile float sink = 0.0F;

  float stage_sum = 0.0F;
  const auto rebuild_started = std::chrono::steady_clock::now();
  for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
    for (std::size_t index = 0; index < boards.size(); ++index) {
      refresh_perspective_accumulator(network, boards[index], Color::White,
                                      scratch[index].accumulator.white);
      refresh_perspective_accumulator(network, boards[index], Color::Black,
                                      scratch[index].accumulator.black);
      stage_sum += scratch[index].accumulator.white[0] + scratch[index].accumulator.black[0];
    }
  }
  const auto rebuild_elapsed = std::chrono::steady_clock::now() - rebuild_started;
  sink = sink + stage_sum;

  stage_sum = 0.0F;
  const auto clip_started = std::chrono::steady_clock::now();
  for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
    for (NnueEvaluatorStageScratch& value : scratch) {
      for (std::size_t i = 0; i < kAccumulator; ++i) {
        value.clipped_white[i] = clipped_relu(value.accumulator.white[i]);
        value.clipped_black[i] = clipped_relu(value.accumulator.black[i]);
      }
      stage_sum += value.clipped_white[0] + value.clipped_black[0];
    }
  }
  const auto clip_elapsed = std::chrono::steady_clock::now() - clip_started;
  sink = sink + stage_sum;

  stage_sum = 0.0F;
  const auto hidden1_started = std::chrono::steady_clock::now();
  for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
    for (std::size_t index = 0; index < boards.size(); ++index) {
      NnueEvaluatorStageScratch& value = scratch[index];
      const bool white_to_move = boards[index].side_to_move() == Color::White;
      const float* stm = white_to_move ? value.clipped_white.data() : value.clipped_black.data();
      const float* opp = white_to_move ? value.clipped_black.data() : value.clipped_white.data();
      hidden1_dense(network, stm, opp, value.hidden1_sums.data());
      stage_sum += value.hidden1_sums[0];
    }
  }
  const auto hidden1_elapsed = std::chrono::steady_clock::now() - hidden1_started;
  sink = sink + stage_sum;

  stage_sum = 0.0F;
  const auto hidden1_activation_started = std::chrono::steady_clock::now();
  for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
    for (NnueEvaluatorStageScratch& value : scratch) {
      for (std::size_t i = 0; i < network.hidden1_dimensions; ++i)
        value.hidden1[i] = clipped_relu(value.hidden1_sums[i]);
      stage_sum += value.hidden1[0];
    }
  }
  const auto hidden1_activation_elapsed =
      std::chrono::steady_clock::now() - hidden1_activation_started;
  sink = sink + stage_sum;

  stage_sum = 0.0F;
  const auto hidden2_started = std::chrono::steady_clock::now();
  for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
    for (NnueEvaluatorStageScratch& value : scratch) {
      for (std::size_t o = 0; o < network.hidden2_dimensions; ++o) {
        float sum = network.hidden2_bias[o];
        const float* row = network.hidden2.data() + o * network.hidden1_dimensions;
        for (std::size_t i = 0; i < network.hidden1_dimensions; ++i)
          sum += row[i] * value.hidden1[i];
        value.hidden2[o] = final_hidden_relu(sum, network.final_hidden_activation);
      }
      stage_sum += value.hidden2[0];
    }
  }
  const auto hidden2_elapsed = std::chrono::steady_clock::now() - hidden2_started;
  sink = sink + stage_sum;

  stage_sum = 0.0F;
  const auto output_started = std::chrono::steady_clock::now();
  for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
    for (NnueEvaluatorStageScratch& value : scratch) {
      float score = network.output_bias;
      for (std::size_t i = 0; i < network.hidden2_dimensions; ++i)
        score += network.output[i] * value.hidden2[i];
      stage_sum += score * network.output_scale;
    }
  }
  const auto output_elapsed = std::chrono::steady_clock::now() - output_started;
  sink = sink + stage_sum;
  (void)sink;

  return NnueEvaluatorStageProfile{
      profile_us_per_evaluation(rebuild_elapsed, boards.size(), repeats),
      profile_us_per_evaluation(clip_elapsed, boards.size(), repeats),
      profile_us_per_evaluation(hidden1_elapsed, boards.size(), repeats),
      profile_us_per_evaluation(hidden1_activation_elapsed, boards.size(), repeats),
      profile_us_per_evaluation(hidden2_elapsed, boards.size(), repeats),
      profile_us_per_evaluation(output_elapsed, boards.size(), repeats)};
}
#endif

#if defined(HEBICHESS_NNUE_TEST_REFERENCE)
namespace {

std::optional<std::vector<float>> evaluate_nnue_hidden1_pre_impl(
    const Board& board, bool selected_variant) noexcept {
  const auto& maybe = loaded_network();
  if (!maybe) return std::nullopt;
  const Network& network = *maybe;
  NnueAccumulator accumulator;
  refresh_perspective_accumulator(network, board, Color::White, accumulator.white);
  refresh_perspective_accumulator(network, board, Color::Black, accumulator.black);
  const auto& stm = board.side_to_move() == Color::White ? accumulator.white : accumulator.black;
  const auto& opp = board.side_to_move() == Color::White ? accumulator.black : accumulator.white;
  AccumulatorArray clipped_stm{};
  AccumulatorArray clipped_opp{};
  for (std::size_t i = 0; i < kAccumulator; ++i) {
    clipped_stm[i] = clipped_relu(stm[i]);
    clipped_opp[i] = clipped_relu(opp[i]);
  }
  std::vector<float> values(network.hidden1_dimensions);
  if (selected_variant)
    hidden1_dense(network, clipped_stm.data(), clipped_opp.data(), values.data());
  else
    hidden1_dense_legacy(network, clipped_stm.data(), clipped_opp.data(), values.data());
  return values;
}

}  // namespace

std::optional<std::vector<float>> evaluate_nnue_hidden1_pre_active_for_test(
    const Board& board) noexcept {
  return evaluate_nnue_hidden1_pre_impl(board, true);
}

std::optional<std::vector<float>> evaluate_nnue_hidden1_pre_legacy_for_test(
    const Board& board) noexcept {
  return evaluate_nnue_hidden1_pre_impl(board, false);
}

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
