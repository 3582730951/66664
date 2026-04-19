#pragma once

#include <cstddef>
#include <cstdint>

namespace vmp::runtime::integrity {

std::uint32_t crc32_compute(const void* data, std::size_t len);

class Crc32Stream {
 public:
  Crc32Stream() noexcept;

  void update(const void* data, std::size_t len);
  std::uint32_t value() const noexcept;
  void reset() noexcept;

 private:
  std::uint32_t state_ = 0xFFFFFFFFu;
};

}  // namespace vmp::runtime::integrity
