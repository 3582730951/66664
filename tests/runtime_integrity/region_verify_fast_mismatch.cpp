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
  const auto audit_path = temp_path("region_fast_mismatch", ".log");
  audit::AuditWriter writer(audit_path);
  auto& runtime = state::RuntimeState::instance();
  runtime.shutdown();
  runtime.init_once(&writer, {"linux", "test", false, 2000});

  std::array<std::uint8_t, 8> bytes{{1,2,3,4,5,6,7,8}};
  auto& registry = integrity::RegionRegistry::instance();
  registry.reset_for_tests();

  integrity::ProtectedRegion region{};
  region.name = "fast_mismatch";
  region.base = bytes.data();
  region.size = bytes.size();
  registry.register_region(region);

  bytes[5] ^= 0x55u;
  const auto result = registry.verify_one_fast("fast_mismatch");
  writer.flush();

  require(result.status == integrity::RegionVerifyStatus::mismatch, "fast verify must detect CRC32 mismatch");
  require(result.mode == integrity::RegionRegistry::Mode::fast, "result mode must be fast");
  require(!result.crc32_match, "fast verify must report CRC32 mismatch");
  require(runtime.current_state() == state::RuntimeStateValue::ready, "fast verify must not degrade runtime state");
  require(read_all(audit_path).find("integrity_fast_mismatch") != std::string::npos, "missing integrity_fast_mismatch audit");

  runtime.shutdown();
  std::cout << "region_verify_fast_mismatch OK\n";
  return 0;
}
