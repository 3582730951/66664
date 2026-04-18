#include "test_common.h"

#include <vmp/runtime/state/hot_recorder.h>
#include <vmp/runtime/state/profile.h>
#include <vmp/runtime/state/scheduler.h>

#include <iostream>

using namespace vmp::tests::runtime_state;

int main() {
  vmp::runtime::state::HotRecorder recorder;
  for (int i = 0; i < 500; ++i) {
    recorder.record_function_entry(1, 0x20);
  }
  auto online = recorder.snapshot();
  online.uptime_seconds = 120;
  vmp::runtime::state::OfflineProfile offline;
  offline.meta["schema"] = "vp1";
  auto fused = vmp::runtime::state::fuse_profiles(offline, online, 0.4);

  vmp::runtime::state::HotScheduler scheduler;
  vmp::runtime::state::SchedulerInput input;
  input.modules[1].current_budget_bytes = 2u * 1024u * 1024u;
  input.modules[1].current_cache_bytes = 0;
  const auto actions = scheduler.make_actions(fused, online, input);
  bool found = false;
  for (const auto& action : actions) {
    if (action.kind == vmp::runtime::state::ScheduleActionKind::jit_compile_now && action.module_id == 1 && action.pc == 0x20) {
      found = true;
    }
  }
  require(found, "expected jit_compile_now action");
  std::cout << "scheduler_chooses_jit_when_hot OK\n";
}
