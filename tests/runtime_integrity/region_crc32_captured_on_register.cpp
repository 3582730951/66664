#include "test_common.h"

#include <array>
#include <iostream>

#include <vmp/runtime/integrity/crc32.h>
#include <vmp/runtime/integrity/integrity.h>

using namespace vmp::tests::runtime_integrity;
namespace integrity = vmp::runtime::integrity;

int main() {
  std::array<std::uint8_t, 8> bytes{{1,2,3,4,5,6,7,8}};
  auto& registry = integrity::RegionRegistry::instance();
  registry.reset_for_tests();

  integrity::ProtectedRegion region{};
  region.name = "captured_crc32";
  region.base = bytes.data();
  region.size = bytes.size();
  registry.register_region(region);

  const auto regions = registry.all();
  require(regions.size() == 1, "expected one registered region");
  require(regions[0].expected_crc32.has_value(), "expected_crc32 must be captured at register time");
  require(*regions[0].expected_crc32 == integrity::crc32_compute(bytes.data(), bytes.size()), "captured CRC32 mismatch");

  std::cout << "region_crc32_captured_on_register OK\n";
  return 0;
}
