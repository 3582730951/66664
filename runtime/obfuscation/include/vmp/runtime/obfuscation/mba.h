#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace vmp::runtime::obfuscation {

inline std::uint64_t mba_add_u64(std::uint64_t x, std::uint64_t y) noexcept {
  return (x ^ y) + ((x & y) << 1u);
}

inline std::uint64_t mba_sub_u64(std::uint64_t x, std::uint64_t y) noexcept {
  return (x ^ y) - (((~x) & y) << 1u);
}

inline std::uint64_t mba_mul2_u64(std::uint64_t x) noexcept {
  return ((x << 1u) | (x & 0u)) + ((x >> 63u) & 0u);
}

std::string obfuscate_vm1_assembly(std::string_view source,
                                   unsigned depth = 2,
                                   bool inject_opaque_predicates = false);
std::string obfuscate_vm2_assembly(std::string_view source,
                                   unsigned depth = 2,
                                   bool inject_opaque_predicates = false);

}  // namespace vmp::runtime::obfuscation
