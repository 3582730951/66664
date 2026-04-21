#pragma once

#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>

#include <vmp/runtime/obfuscation/mba.h>
#include <vmp/runtime/obfuscation/opaque.h>
#include <vmp/runtime/vm1/vm1.h>
#include <vmp/runtime/vm2/vm2.h>

namespace vmp::tests::runtime_obfuscation {

inline void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

inline std::mt19937_64 make_rng() {
  return std::mt19937_64{0x40BfCAFE12345678ull};
}

}  // namespace vmp::tests::runtime_obfuscation
