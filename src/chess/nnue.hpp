#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "chess/board.hpp"

namespace hebichess {

constexpr std::size_t kNnueAccumulatorDimensions = 256;

struct NnueAccumulator {
  std::array<float, kNnueAccumulatorDimensions> white{};
  std::array<float, kNnueAccumulatorDimensions> black{};
};

bool load_nnue_network(const std::string& path, std::string& error);
// Uses the same strict v1/v2/v3 parser as the file loader.  The browser
// bridge supplies an ArrayBuffer through WASM linear memory with this entry.
bool load_nnue_network_bytes(const std::uint8_t* bytes, std::size_t size, std::string& error);
bool nnue_network_available() noexcept;
// Rebuilds both perspective accumulators from the loaded network's transform
// bias and the current board's active features.
bool refresh_nnue_accumulator(const Board& board, NnueAccumulator& accumulator) noexcept;
// Builds a child accumulator from a parent board/accumulator and one legal
// move.  A king-relative perspective is refreshed only when that king moves.
bool update_nnue_accumulator(const Board& parent, const Move& move,
                             const NnueAccumulator& parent_accumulator,
                             NnueAccumulator& child_accumulator) noexcept;
// Float diagnostic used by parity tooling. Search uses the rounded int API.
std::optional<float> evaluate_nnue_network_raw(const Board& board) noexcept;
// Dense NNUE forward from a caller-owned accumulator.  This API does not
// mutate global scratch and is intended for the future search-local stack.
std::optional<float> evaluate_nnue_network_raw_from_accumulator(
    const Board& board, const NnueAccumulator& accumulator) noexcept;
#if defined(HEBICHESS_NNUE_TEST_PROFILE)
// Compiled only into the accumulator benchmark target.  This deliberately
// profiles the evaluator as separate stages without instrumenting the normal
// production evaluator path.
struct NnueEvaluatorStageProfile {
  double accumulator_rebuild_us;
  double clip_precompute_us;
  double hidden1_dense_us;
  double hidden1_activation_hidden2_us;
  double output_us;
};

std::optional<NnueEvaluatorStageProfile> profile_nnue_evaluator_stages(
    const std::vector<Board>& boards, std::size_t repeats) noexcept;
#endif
#if defined(HEBICHESS_NNUE_TEST_REFERENCE)
// Test-only legacy implementation for direct old-versus-new raw-score parity.
std::optional<float> evaluate_nnue_network_raw_reference(const Board& board) noexcept;
#endif
std::optional<int> evaluate_nnue_network(const Board& board) noexcept;
void clear_nnue_network() noexcept;

}  // namespace hebichess
