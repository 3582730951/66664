#pragma once

#include <stdexcept>
#include <string>

namespace vmp::tests::strings {

inline void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

}  // namespace vmp::tests::strings
