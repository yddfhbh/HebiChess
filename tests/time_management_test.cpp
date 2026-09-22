#include <cassert>

#include "chess/time_management.hpp"

using namespace hebichess;

int main() {
  const TimeBudget one_plus_zero = allocate_time_budget(60'000, 0);
  const TimeBudget five_plus_zero = allocate_time_budget(300'000, 0);
  const TimeBudget ten_plus_five = allocate_time_budget(600'000, 5'000);
  assert(one_plus_zero.soft_ms < five_plus_zero.soft_ms);
  assert(five_plus_zero.soft_ms < ten_plus_five.soft_ms);
  assert(ten_plus_five.soft_ms <= 8'000);
  assert(ten_plus_five.hard_ms <= 20'000);

  const TimeBudget unlimited = allocate_time_budget(0, 0, true);
  assert(unlimited.soft_ms == 5'000 && unlimited.hard_ms == 15'000);

  assert(should_stop_at_soft_deadline({3, 10, 60, false}, false, true));
  assert(should_stop_at_soft_deadline({2, 20, 35, false}, true));
  assert(!should_stop_at_soft_deadline({2, 10, 60, false}, false, true));
  assert(!should_stop_at_soft_deadline({1, 10, 60, false}, true, true));
  assert(!should_stop_at_soft_deadline({3, 50, 60, false}, true, true));
  assert(!should_stop_at_soft_deadline({3, 10, 60, true}, true, true));
  assert(should_stop_at_soft_deadline({3, 10, 60, false}, true, true));
  return 0;
}
