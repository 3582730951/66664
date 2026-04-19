#include "test_common.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <vector>

#include <vmp/runtime/audit/audit.h>
#include <vmp/runtime/integrity/integrity.h>
#include <vmp/runtime/state/state.h>
#include <vmp/runtime/strings/cipher.h>

using namespace vmp::tests::runtime_integrity;
namespace audit = vmp::runtime::audit;
namespace integrity = vmp::runtime::integrity;
namespace state = vmp::runtime::state;
namespace strings = vmp::runtime::strings;

int main() {
  const auto audit_path = temp_path("region_escalation_fast_only", ".log");
  audit::AuditWriter writer(audit_path);
  auto& runtime = state::RuntimeState::instance();
  runtime.shutdown();
  runtime.init_once(&writer, {"linux", "test", false, 2000});

  std::array<std::uint8_t, 8> bytes{{9,8,7,6,5,4,3,2}};
  auto digest = strings::sha256(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));

  auto& registry = integrity::RegionRegistry::instance();
  registry.reset_for_tests();
  integrity::ProtectedRegion region{};
  region.name = "fast_only_escalation";
  region.base = bytes.data();
  region.size = bytes.size();
  std::copy_n(digest.begin(), 32, region.expected_sha256);
  region.expected_crc32 = 0xDEADBEEFu;
  registry.register_region(region);

  const auto fast = registry.verify_one_fast("fast_only_escalation");
  const auto authoritative = registry.verify_one("fast_only_escalation", integrity::RegionRegistry::Mode::authoritative);
  writer.flush();

  require(fast.status == integrity::RegionVerifyStatus::mismatch, "fast verification should mismatch");
  require(authoritative.status == integrity::RegionVerifyStatus::ok, "authoritative verification should recover via SHA-256");
  require(authoritative.sha256_checked, "authoritative verification must check SHA-256");
  require(authoritative.sha256_match, "SHA-256 should match original content");
  require(runtime.current_state() == state::RuntimeStateValue::ready, "runtime must remain ready when authoritative verification passes");

  const auto log = read_all(audit_path);
  require(log.find("integrity_fast_mismatch") != std::string::npos, "fast mismatch audit missing");
  require(log.find("integrity_authoritative_mismatch") == std::string::npos, "authoritative mismatch audit must be absent");
  require(log.find("integrity_failed") == std::string::npos, "integrity_failed must not be observed");

  runtime.shutdown();
  std::cout << "region_escalation_fast_mismatch_authoritative_ok OK\n";
  return 0;
}
