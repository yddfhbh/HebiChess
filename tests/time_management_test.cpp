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

  // A clear choice is only actionable after the conservative 75% window.
  assert(should_stop_at_soft_deadline({4, 10, 75, true, false}, false, true));
  assert(!should_stop_at_soft_deadline({4, 10, 75, false, false}, false, true));
  assert(!should_stop_at_soft_deadline({3, 10, 75, true, false}, false, true));
  assert(!should_stop_at_soft_deadline({4, 16, 75, true, false}, false, true));

  // Unknown margin must not turn a stable best move into a clear choice.
  assert(!should_stop_at_soft_deadline({4, 0, 0, false, false}, false, true));
  assert(!should_stop_at_soft_deadline({4, 0, 80, false, false}, true));
  // A proven small margin allows ordinary soft-deadline stopping only.
  assert(should_stop_at_soft_deadline({2, 20, 35, true, false}, true));
  assert(!should_stop_at_soft_deadline({2, 20, 34, true, false}, true));
  assert(!should_stop_at_soft_deadline({2, 20, 35, false, false}, true));
  assert(!should_stop_at_soft_deadline({2, 20, 80, true, true}, true));
  assert(!should_stop_at_soft_deadline({4, 10, 80, true, false}, false, true));
  return 0;
}
