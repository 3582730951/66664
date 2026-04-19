#include <vmp/runtime/integrity/crc32.h>

#include <array>
#include <stdexcept>

namespace vmp::runtime::integrity {
namespace {

constexpr std::uint32_t crc32_table_entry(std::uint32_t index) noexcept {
  std::uint32_t value = index;
  for (int bit = 0; bit < 8; ++bit) {
    if ((value & 1u) != 0u) {
      value = (value >> 1u) ^ 0xEDB88320u;
    } else {
      value >>= 1u;
    }
  }
  return value;
}

constexpr std::array<std::uint32_t, 256> make_crc32_table() noexcept {
  std::array<std::uint32_t, 256> table{};
  for (std::size_t i = 0; i < table.size(); ++i) {
    table[i] = crc32_table_entry(static_cast<std::uint32_t>(i));
  }
  return table;
}

constexpr auto kCrc32Table = make_crc32_table();

}  // namespace

std::uint32_t crc32_compute(const void* data, std::size_t len) {
  Crc32Stream stream;
  stream.update(data, len);
  return stream.value();
}

Crc32Stream::Crc32Stream() noexcept = default;

void Crc32Stream::update(const void* data, std::size_t len) {
  if (len == 0) {
    return;
  }
  if (data == nullptr) {
    throw std::invalid_argument("crc32: null data with non-zero length");
  }
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  for (std::size_t i = 0; i < len; ++i) {
    const auto index = static_cast<std::uint8_t>((state_ ^ bytes[i]) & 0xFFu);
    state_ = (state_ >> 8u) ^ kCrc32Table[index];
  }
}

std::uint32_t Crc32Stream::value() const noexcept { return state_ ^ 0xFFFFFFFFu; }

void Crc32Stream::reset() noexcept { state_ = 0xFFFFFFFFu; }

}  // namespace vmp::runtime::integrity
