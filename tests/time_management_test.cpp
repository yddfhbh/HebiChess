#include <cassert>

#include "chess/time_management.hpp"

using namespace hebichess;

int main() {
  assert(allocate_time_budget(60'000, 0).soft_ms == 600);
  assert(allocate_time_budget(300'000, 0).soft_ms == 3000);
  assert(allocate_time_budget(600'000, 5'000).soft_ms == 5000);
  assert(allocate_time_budget(600'000, 5'000).hard_ms == 20000);
  assert(allocate_time_budget(0, 0, true).soft_ms == 5000);
  assert(allocate_time_budget(0, 0, true).hard_ms == 20000);

  const TimeEvidence high{{1, 1, 1}, {100, 105, 110}, {false, false, false}, true, 80};
  const TimeEvidence medium{{1, 1, 1}, {100, 115, 125}, {false, false, false}, true, 50};
  const TimeEvidence low{{1, 2, 2}, {100, 145, 180}, {false, false, false}, true, 35};
  const TimeEvidence very_low{{1, 2, 3}, {100, 170, 240}, {false, true, false}, true, 15};
  assert(choose_time_confidence(high) == TimeConfidence::High);
  assert(choose_time_confidence(medium) == TimeConfidence::Medium);
  assert(choose_time_confidence(low) == TimeConfidence::Low);
  assert(choose_time_confidence(very_low) == TimeConfidence::VeryLow);
  assert(adaptive_target_ms(3000, 12000, TimeConfidence::High) == 3000);
  assert(adaptive_target_ms(3000, 12000, TimeConfidence::Medium) == 4500);
  assert(adaptive_target_ms(3000, 12000, TimeConfidence::Low) == 6000);
  assert(adaptive_target_ms(3000, 5000, TimeConfidence::VeryLow) == 5000);

  assert(update_adaptive_target_floor(0, 3000, 12000,
                                     TimeConfidence::High, false) == 0);
  assert(update_adaptive_target_floor(0, 3000, 12000,
                                     TimeConfidence::High, true) == 3000);
  assert(update_adaptive_target_floor(0, 3000, 12000,
                                     TimeConfidence::Medium, true) == 4500);
  assert(update_adaptive_target_floor(0, 3000, 12000,
                                     TimeConfidence::Low, true) == 6000);
  assert(update_adaptive_target_floor(0, 3000, 12000,
                                     TimeConfidence::VeryLow, true) == 7500);
  assert(update_adaptive_target_floor(3000, 3000, 12000,
                                     TimeConfidence::VeryLow, true) == 7500);

  assert(choose_time_confidence({{1, 1, 2}, {100, 105, 110}, {false, false, false}, true, 80}) != TimeConfidence::High);
  assert(choose_time_confidence({{1, 1, 1}, {100, 145, 180}, {false, false, false}, true, 80}) != TimeConfidence::Medium);
  assert(choose_time_confidence({{1, 1, 1}, {100, 105, 110}, {false, false, false}, false, 0}) != TimeConfidence::High);
  assert(choose_time_confidence({{1, 1, 1}, {100, 105, 110}, {false, false, false}, true, 15}) != TimeConfidence::High);
  assert(choose_time_confidence({{1, 1, 1}, {100, 105, 110}, {false, true, false}, true, 80}) != TimeConfidence::High);
  return 0;
}
