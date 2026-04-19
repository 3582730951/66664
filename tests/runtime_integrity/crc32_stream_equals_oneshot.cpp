#include "test_common.h"

#include <array>
#include <cstdint>
#include <iostream>

#include <vmp/runtime/integrity/crc32.h>

using namespace vmp::tests::runtime_integrity;
namespace integrity = vmp::runtime::integrity;

int main() {
  std::array<std::uint8_t, 512> buffer{};
  for (std::size_t i = 0; i < buffer.size(); ++i) {
    buffer[i] = static_cast<std::uint8_t>((i * 29u + 7u) & 0xFFu);
  }

  integrity::Crc32Stream stream;
  stream.update(buffer.data(), 13);
  stream.update(buffer.data() + 13, 111);
  stream.update(buffer.data() + 124, 5);
  stream.update(buffer.data() + 129, buffer.size() - 129);

  const auto one_shot = integrity::crc32_compute(buffer.data(), buffer.size());
  require(stream.value() == one_shot, "streaming CRC32 must match one-shot result");

  stream.reset();
  stream.update(buffer.data(), buffer.size());
  require(stream.value() == one_shot, "reset() must restore initial stream state");

  std::cout << "crc32_stream_equals_oneshot OK\n";
  return 0;
}
