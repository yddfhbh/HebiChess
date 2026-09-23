#pragma once

#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdint>

namespace hebichess {

struct TimeBudget {
  int soft_ms{0};
  int hard_ms{0};
};

inline TimeBudget allocate_time_budget(int remaining_ms, int increment_ms,
                                       bool unlimited = false,
                                       std::uint32_t fullmove_number = 16) noexcept {
  const bool early_budget = fullmove_number <= 15;
  if (unlimited) {
    const int hard = early_budget ? 15000 : 18000;
    return {4500, hard};
  }
  remaining_ms = std::max(0, remaining_ms);
  increment_ms = std::max(0, increment_ms);
  const int safe_remaining = std::max(1, remaining_ms -
      std::max(20, remaining_ms / 10));
  int soft = remaining_ms / 100 + (increment_ms * 40) / 100;
  soft = std::clamp(soft, 300, 5000);
  soft = std::max(300, soft * 9 / 10);
  soft = std::min(soft, safe_remaining);
  const int requested_hard = early_budget
      ? std::max(soft * 5, soft + 4000)
      : std::max(soft * 4, soft + 3000);
  const int cap = early_budget ? 15000 : 20000;
  return {soft, std::min({requested_hard, cap, safe_remaining})};
}

enum class TimeConfidence { High, Medium, Low, VeryLow };

struct TimeEvidence {
  // These histories contain completed root iterations, newest last.
  std::vector<int> best_move_history;
  std::vector<int> score_history;
  std::vector<bool> aspiration_retry_history;
  bool root_margin_known{false};
  int root_margin{0};
};

inline TimeConfidence choose_time_confidence(const TimeEvidence& evidence) noexcept {
  const std::size_t n = evidence.best_move_history.size();
  const std::size_t score_count = evidence.score_history.size();
  const std::size_t recent = std::min<std::size_t>(3, n);
  bool recent_move_change = false;
  if (recent >= 2) {
    for (std::size_t i = n - recent + 1; i < n; ++i)
      recent_move_change |= evidence.best_move_history[i] != evidence.best_move_history[i - 1];
  }
  int swing = 0;
  if (score_count >= 2) {
    const std::size_t first = score_count - std::min<std::size_t>(3, score_count);
    auto range = std::minmax_element(evidence.score_history.begin() + first,
                                     evidence.score_history.end());
    swing = *range.second - *range.first;
  }
  bool retry = false;
  const std::size_t retry_count = evidence.aspiration_retry_history.size();
  for (std::size_t i = retry_count - std::min<std::size_t>(3, retry_count);
       i < retry_count; ++i) retry |= evidence.aspiration_retry_history[i];
  const bool small_margin = evidence.root_margin_known && evidence.root_margin <= 20;
  const bool very_low = (recent_move_change && swing >= 60) ||
      (retry && (recent_move_change || swing >= 30 || small_margin)) ||
      (swing >= 60 && small_margin);
  if (very_low) return TimeConfidence::VeryLow;
  if (recent_move_change || !evidence.root_margin_known ||
      evidence.root_margin <= 35 || swing >= 35 || retry)
    return TimeConfidence::Low;
  const bool stable_moves = n >= 3 && !recent_move_change;
  if (stable_moves && score_count >= 3 && swing <= 15 &&
      evidence.root_margin_known && evidence.root_margin >= 75 && !retry)
    return TimeConfidence::High;
  if (n >= 3 && score_count >= 3 && swing <= 30 &&
      evidence.root_margin_known && evidence.root_margin >= 40 && !retry)
    return TimeConfidence::Medium;
  return TimeConfidence::Low;
}

inline double time_multiplier(TimeConfidence confidence) noexcept {
  switch (confidence) {
    case TimeConfidence::High: return 1.0;
    case TimeConfidence::Medium: return 1.5;
    case TimeConfidence::Low: return 2.0;
    case TimeConfidence::VeryLow: return 2.5;
  }
  return 2.0;
}

inline int adaptive_target_ms(int soft_ms, int hard_ms,
                              TimeConfidence confidence) noexcept {
  return std::min(hard_ms, std::max(0, static_cast<int>(std::ceil(
      soft_ms * time_multiplier(confidence)))));
}

inline int update_adaptive_target_floor(int current_floor_ms, int soft_ms,
                                        int hard_ms, TimeConfidence confidence,
                                        bool evidence_ready) noexcept {
  if (!evidence_ready) return current_floor_ms;
  return std::max(current_floor_ms,
                  adaptive_target_ms(soft_ms, hard_ms, confidence));
}

}  // namespace hebichess
