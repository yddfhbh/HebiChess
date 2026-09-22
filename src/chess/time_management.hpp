#pragma once

#include <algorithm>
#include <chrono>
#include <cstdlib>

namespace hebichess {

struct TimeBudget {
  int soft_ms{0};
  int hard_ms{0};
};

// Clock allocation is deliberately independent of the search so it can be
// tested without waiting on a wall clock.
inline TimeBudget allocate_time_budget(int remaining_ms, int increment_ms,
                                       bool unlimited = false) noexcept {
  if (unlimited) return {5000, 15000};
  remaining_ms = std::max(0, remaining_ms);
  increment_ms = std::max(0, increment_ms);
  const int safe_remaining = std::max(1, remaining_ms -
      std::max(20, remaining_ms / 10));
  int soft = remaining_ms / 100 + (increment_ms * 40) / 100;
  soft = std::clamp(soft, 300, 8000);
  soft = std::min(soft, safe_remaining);
  const int requested_hard = std::max(soft * 5 / 2, soft + 1500);
  return {soft, std::min({requested_hard, 20000, safe_remaining})};
}

struct SoftStopState {
  int best_move_stability{0};
  int score_swing{0};
  int root_margin{0};
  bool aspiration_retry{false};
};

inline bool should_stop_at_soft_deadline(const SoftStopState& state,
                                         bool soft_reached,
                                         bool clear_choice_window = false) noexcept {
  if (state.aspiration_retry) return false;
  if (clear_choice_window && state.best_move_stability >= 3 &&
      state.score_swing <= 15 && state.root_margin >= 60) return true;
  if (!soft_reached) return false;
  return state.best_move_stability >= 2 && state.score_swing <= 20 &&
      state.root_margin >= 35;
}

}  // namespace hebichess
