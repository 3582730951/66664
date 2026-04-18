#include "test_common.h"

#include <vmp/runtime/audit/audit.h>
#include <vmp/runtime/state/profile.h>
#include <vmp/runtime/state/scheduler.h>
#include <vmp/runtime/state/state.h>

#include <iostream>

using namespace vmp::tests::runtime_state;

int main() {
  const auto audit_path = temp_path("scheduler_audit", ".log");
  vmp::runtime::audit::AuditWriter writer(audit_path);
  auto& state = vmp::runtime::state::RuntimeState::instance();
  state.shutdown();
  require(state.init_once(&writer, {"linux", "test", false}), "init failed");

  vmp::runtime::state::OfflineProfile profile;
  profile.meta["schema"] = "vp1";
  profile.entries.push_back({1, 0x20, 500, 3, 1.0});
  vmp::runtime::state::HotRecorderSnapshot online;
  online.uptime_seconds = 120;
  const auto fused = vmp::runtime::state::fuse_profiles(profile, online, 0.4);

  vmp::runtime::state::HotScheduler scheduler;
  vmp::runtime::state::SchedulerInput input;
  input.modules[1].current_budget_bytes = 2u * 1024u * 1024u;
  const auto actions = scheduler.make_actions(fused, online, input, &state);
  require(!actions.empty(), "expected decisions");
  writer.flush();
  const auto log = read_all(audit_path);
  require(log.find("scheduler_decision") != std::string::npos, "scheduler_decision not logged");
  state.shutdown();
  std::cout << "audit_decision_logged OK\n";
}
