#include <vmp/runtime/obfuscation/opaque.h>

namespace vmp::runtime::obfuscation {

std::uint64_t opaque_handler_mix(std::uint64_t value, std::uint64_t salt) noexcept {
  value ^= salt + 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
  value ^= value >> 30u;
  value *= 0xbf58476d1ce4e5b9ull;
  value ^= value >> 27u;
  value *= 0x94d049bb133111ebull;
  value ^= value >> 31u;
  return value;
}

}  // namespace vmp::runtime::obfuscation
