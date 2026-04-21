#pragma once

#include <cstdint>

namespace vmp::runtime::obfuscation {

inline bool opaque_even_product_predicate(std::uint64_t value) noexcept {
  return ((value * (value + 1u)) & 1u) == 0u;
}

std::uint64_t opaque_handler_mix(std::uint64_t value, std::uint64_t salt) noexcept;

}  // namespace vmp::runtime::obfuscation
