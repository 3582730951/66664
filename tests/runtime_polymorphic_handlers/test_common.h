#pragma once

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace vmp::tests::runtime_polymorphic_handlers {

inline void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Layout, typename Sequence>
inline void require_permutation(const Layout& layout, const Sequence& canonical, const std::string& label) {
  require(layout.entries.size() == canonical.size(), label + ": entry count mismatch");
  std::vector<std::uint16_t> seen;
  seen.reserve(layout.entries.size());
  bool changed = false;
  for (std::size_t i = 0; i < layout.entries.size(); ++i) {
    seen.push_back(static_cast<std::uint16_t>(layout.entries[i].opcode));
    if (static_cast<std::uint16_t>(layout.entries[i].opcode) != static_cast<std::uint16_t>(canonical[i])) {
      changed = true;
    }
    require(layout.entries[i].junk_length >= 4 && layout.entries[i].junk_length <= 32,
            label + ": junk length out of range");
    require(layout.entries[i].variant <= 2, label + ": variant id out of range");
    require(layout.entries[i].entry != nullptr, label + ": null entry pointer");
  }
  require(changed, label + ": expected shuffled handler order");
  std::sort(seen.begin(), seen.end());
  require(std::adjacent_find(seen.begin(), seen.end()) == seen.end(), label + ": duplicate opcode in layout");
}

inline std::uint64_t fnv1a64(const std::uint8_t* bytes, std::size_t size) {
  std::uint64_t hash = 1469598103934665603ull;
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 1099511628211ull;
  }
  return hash;
}

inline std::uint64_t code_prefix_hash(const void* ptr, std::size_t bytes = 32) {
  auto* begin = reinterpret_cast<const std::uint8_t*>(ptr);
  return fnv1a64(begin, bytes);
}

}  // namespace vmp::tests::runtime_polymorphic_handlers
