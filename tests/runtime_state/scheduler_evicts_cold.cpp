#include "test_common.h"

#include <vmp/runtime/state/hot_recorder.h>
#include <vmp/runtime/state/profile.h>
#include <vmp/runtime/state/scheduler.h>

#include <iostream>

using namespace vmp::tests::runtime_state;

int main() {
  vmp::runtime::state::OfflineProfile profile;
  profile.meta["schema"] = "vp1";
  profile.entries.push_back({1, 0x10, 1, 0, 0.01});
  profile.entries.push_back({1, 0x20, 500, 3, 0.9});

  vmp::runtime::state::HotRecorderSnapshot online;
  online.uptime_seconds = 120;
  auto fused = vmp::runtime::state::fuse_profiles(profile, online, 0.4);

  vmp::runtime::state::HotScheduler scheduler;
  vmp::runtime::state::SchedulerInput input;
  input.modules[1].current_budget_bytes = 1u * 1024u * 1024u;
  input.modules[1].current_cache_bytes = 1024u * 1024u;
  const auto actions = scheduler.make_actions(fused, online, input);
  bool found = false;
  for (const auto& action : actions) {
    if (action.kind == vmp::runtime::state::ScheduleActionKind::jit_evict && action.module_id == 1 && action.pc == 0x10) {
      found = true;
    }
  }
  require(found, "expected jit_evict action");
  std::cout << "scheduler_evicts_cold OK\n";
}
