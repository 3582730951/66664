#include "test_common.h"

#include <vmp/policy/policy_ir.h>
#include <vmp/runtime/state/profile.h>

#include <iostream>

using namespace vmp::tests::runtime_state;

int main() {
  vmp::policy::PolicyIR policy;
  vmp::policy::PolicyEntry entry;
  entry.symbol_or_region = "demo";
  entry.protection_domain = vmp::policy::ProtectionDomain::vm1;
  entry.sensitivity_level = vmp::policy::SensitivityLevel::highly_sensitive;
  entry.plaintext_budget = vmp::policy::PlaintextBudget::transient_only;
  entry.integrity_level = vmp::policy::IntegrityLevel::strict;
  entry.reaction_policy = vmp::policy::ReactionPolicy::audit_then_delayed_exit;
  policy.entries.push_back(entry);
  const auto before = policy;

  vmp::runtime::state::OfflineProfile offline;
  offline.meta["schema"] = "vp1";
  offline.entries.push_back({1, 0x10, 1000, 3, 1.0});

  vmp::runtime::state::HotRecorderSnapshot online;
  online.function_hits[{1, 0x10}] = 10000;
  online.uptime_seconds = 120;

  const auto fused = vmp::runtime::state::fuse_profiles(offline, online, 0.4);
  require(!fused.entries.empty(), "fused profile should not be empty");
  require(policy == before, "fusion must not mutate policy ir");
  require(policy.entries.front().protection_domain == vmp::policy::ProtectionDomain::vm1,
          "protection domain changed unexpectedly");
  std::cout << "fusion_does_not_change_policy OK\n";
}
