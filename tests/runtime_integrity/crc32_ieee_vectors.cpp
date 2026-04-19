#include "test_common.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <vmp/runtime/integrity/crc32.h>

using namespace vmp::tests::runtime_integrity;
namespace integrity = vmp::runtime::integrity;

namespace {

std::uint32_t crc32_reference(const std::uint8_t* data, std::size_t len) {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < len; ++i) {
    crc ^= static_cast<std::uint32_t>(data[i]);
    for (int bit = 0; bit < 8; ++bit) {
      const bool lsb = (crc & 1u) != 0u;
      crc >>= 1u;
      if (lsb) {
        crc ^= 0xEDB88320u;
      }
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

}  // namespace

int main() {
  require(integrity::crc32_compute(nullptr, 0) == 0x00000000u, "empty CRC32 must be zero");

  const std::string digits = "123456789";
  require(integrity::crc32_compute(digits.data(), digits.size()) == 0xCBF43926u, "CRC32(123456789) mismatch");

  const std::string one = "a";
  require(integrity::crc32_compute(one.data(), one.size()) == 0xE8B7BE43u, "CRC32(a) mismatch");

  std::array<std::uint8_t, 257> buffer{};
  for (std::size_t i = 0; i < buffer.size(); ++i) {
    buffer[i] = static_cast<std::uint8_t>((i * 73u + 19u) & 0xFFu);
  }
  const auto expected = crc32_reference(buffer.data(), buffer.size());
  require(integrity::crc32_compute(buffer.data(), buffer.size()) == expected, "CRC32(random buffer) mismatch");

  std::cout << "crc32_ieee_vectors OK\n";
  return 0;
}
