#include "test_common.h"

#include <array>
#include <iostream>

#include <vmp/runtime/audit/audit.h>
#include <vmp/runtime/integrity/integrity.h>
#include <vmp/runtime/state/state.h>

using namespace vmp::tests::runtime_integrity;
namespace audit = vmp::runtime::audit;
namespace integrity = vmp::runtime::integrity;
namespace state = vmp::runtime::state;

int main() {
  const auto audit_path = temp_path("region_escalation_both", ".log");
  audit::AuditWriter writer(audit_path);
  auto& runtime = state::RuntimeState::instance();
  runtime.shutdown();
  runtime.init_once(&writer, {"linux", "test", false, 2000});

  std::array<std::uint8_t, 8> bytes{{1,3,5,7,9,11,13,15}};
  auto& registry = integrity::RegionRegistry::instance();
  registry.reset_for_tests();
  integrity::ProtectedRegion region{};
  region.name = "both_mismatch";
  region.base = bytes.data();
  region.size = bytes.size();
  registry.register_region(region);

  bytes[1] ^= 0xFFu;
  const auto fast = registry.verify_one_fast("both_mismatch");
  const auto authoritative = registry.verify_one("both_mismatch", integrity::RegionRegistry::Mode::authoritative);
  writer.flush();

  require(fast.status == integrity::RegionVerifyStatus::mismatch, "fast verify must detect mismatch");
  require(authoritative.status == integrity::RegionVerifyStatus::mismatch, "authoritative verify must confirm mismatch");
  require(runtime.current_state() == state::RuntimeStateValue::degraded, "confirmed mismatch must degrade runtime");

  const auto log = read_all(audit_path);
  require(log.find("integrity_fast_mismatch") != std::string::npos, "fast mismatch audit missing");
  require(log.find("integrity_authoritative_mismatch") != std::string::npos, "authoritative mismatch audit missing");
  require(log.find("integrity_failed") != std::string::npos, "integrity_failed audit missing");
  require(log.find("state_transition") != std::string::npos, "state transition audit missing");

  runtime.shutdown();
  std::cout << "region_escalation_both_mismatch OK\n";
  return 0;
}
