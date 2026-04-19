#include "test_common.h"

#include <array>
#include <iostream>

#include <vmp/runtime/integrity/integrity.h>

using namespace vmp::tests::runtime_integrity;
namespace integrity = vmp::runtime::integrity;

int main() {
  std::array<std::uint8_t, 16> bytes{{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}};
  auto& registry = integrity::RegionRegistry::instance();
  registry.reset_for_tests();

  integrity::ProtectedRegion region{};
  region.name = "fast_ok";
  region.base = bytes.data();
  region.size = bytes.size();
  registry.register_region(region);

  const auto result = registry.verify_one_fast("fast_ok");
  require(result.status == integrity::RegionVerifyStatus::ok, "fast verify should succeed");
  require(result.mode == integrity::RegionRegistry::Mode::fast, "fast verify must report fast mode");
  require(result.crc32_match, "fast verify should report CRC32 match");
  require(!result.sha256_checked, "fast verify should not compute SHA-256");

  std::cout << "region_verify_fast_ok OK\n";
  return 0;
}
