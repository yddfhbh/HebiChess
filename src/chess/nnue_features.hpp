#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "chess/board.hpp"

namespace hebichess {

constexpr std::uint32_t kNnueFeatureSetV1 = 1;
constexpr std::size_t kNnueInputDimensions = 64 * 12 * 64;
constexpr std::size_t kNnueMaxActiveFeatures = 32;

struct NnueFeatures {
  std::array<std::uint32_t, kNnueMaxActiveFeatures> indices{};
  std::size_t size{0};
};

// Perspective-normalized, king-relative HalfKP-v1 features.  The result is
// ordered by physical a1..h8 square scan, which is part of the test ABI.
NnueFeatures extract_nnue_features(const Board& board, Color perspective) noexcept;

}  // namespace hebichess
