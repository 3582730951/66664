#include "test_common.h"

#include <vmp/runtime/state/profile.h>
#include <vmp/runtime/state/scheduler.h>

#include <iostream>

using namespace vmp::tests::runtime_state;

int main() {
  vmp::runtime::state::OfflineProfile profile;
  profile.meta["schema"] = "vp1";
  profile.entries.push_back({1, 0x10, 10000, 3, 1.0});
  profile.entries.push_back({2, 0x10, 1, 0, 0.0});
  vmp::runtime::state::HotRecorderSnapshot online;
  online.uptime_seconds = 120;
  const auto fused = vmp::runtime::state::fuse_profiles(profile, online, 0.4);

  vmp::runtime::state::HotScheduler scheduler;
  vmp::runtime::state::SchedulerInput input;
  input.min_budget_bytes = 1u * 1024u * 1024u;
  input.max_budget_bytes = 16u * 1024u * 1024u;
  input.modules[1].current_budget_bytes = input.min_budget_bytes;
  input.modules[2].current_budget_bytes = input.max_budget_bytes;
  const auto actions = scheduler.make_actions(fused, online, input);
  bool found = false;
  for (const auto& action : actions) {
    if (action.kind == vmp::runtime::state::ScheduleActionKind::cache_resize) {
      require(action.arg >= input.min_budget_bytes, "budget below minimum");
      require(action.arg <= input.max_budget_bytes, "budget above maximum");
      found = true;
    }
  }
  require(found, "expected at least one cache resize action");
  std::cout << "scheduler_caches_resize_within_bounds OK\n";
}
