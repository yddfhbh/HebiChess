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
// Rebuilds both king-relative transform accumulators for a board.
bool refresh_nnue_accumulator(const Board& board, NnueAccumulator& accumulator) noexcept;
// Updates a child accumulator from a legal parent move.  King moves rebuild
// only the affected perspective; all other updates are feature deltas.
bool update_nnue_accumulator(const Board& parent, const Move& move,
                             const NnueAccumulator& parent_accumulator,
                             NnueAccumulator& child_accumulator) noexcept;
// Float diagnostic used by parity tooling. Search uses the rounded int API.
std::optional<float> evaluate_nnue_network_raw(const Board& board) noexcept;
std::optional<float> evaluate_nnue_network_raw_from_accumulator(
    const Board& board, const NnueAccumulator& accumulator) noexcept;

#if defined(HEBICHESS_NNUE_TEST_PROFILE)
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
std::optional<float> evaluate_nnue_network_raw_reference(const Board& board) noexcept;
#endif

std::optional<int> evaluate_nnue_network(const Board& board) noexcept;
void clear_nnue_network() noexcept;

#if defined(HEBICHESS_NNUE_VARIANT_TEST)
// Cross-binary layout acceptance probe.  It compares the compile-time active
// dense layout with Original4 while reusing the loaded model and accumulators.
// This is intentionally unavailable to production and browser targets.
struct NnueVariantParity {
  float hidden1_max_abs_diff{0.0F};
  float raw_max_abs_diff{0.0F};
  int active_cp{0};
  int original4_cp{0};
};

std::optional<NnueVariantParity> nnue_variant_parity_for_test(
    const Board& board) noexcept;
#endif

#ifdef HEBICHESS_NNUE_DIAGNOSTICS
// Test-binary-only evaluator variants used to isolate portable hot-path costs.
// They are intentionally not compiled into normal engine targets.
enum class NnueDiagnosticVariant {
  Legacy,
  AllocationOnly,
  ClipPrecomputeOnly,
  DenseLoopOnly,
  CombinedCurrent,
};

struct NnueDiagnosticScratchArrays {
  std::size_t white_elements, white_bytes;
  std::size_t black_elements, black_bytes;
  std::size_t h1_elements, h1_bytes;
  std::size_t h2_elements, h2_bytes;
  std::size_t clipped_stm_elements, clipped_stm_bytes;
  std::size_t clipped_opp_elements, clipped_opp_bytes;
};

struct NnueDiagnosticAllocationTiming {
  double reserve_only_us;
  double current_vector_setup_us;
};

NnueDiagnosticScratchArrays nnue_diagnostic_scratch_arrays() noexcept;
std::optional<float> evaluate_nnue_network_raw_diagnostic(
    const Board& board, NnueDiagnosticVariant variant) noexcept;
NnueDiagnosticAllocationTiming nnue_diagnostic_measure_allocations(
    std::size_t iterations) noexcept;
#endif

}  // namespace hebichess
