#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <vmp/runtime/obfuscation/timing_trap.h>
#include <vmp/runtime/vm1/vm1.h>
#include <vmp/runtime/vm2/vm2.h>

namespace vmp::tests::runtime_timing_traps {

inline void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

inline std::string read_all(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return {};
  }
  std::ostringstream oss;
  oss << input.rdbuf();
  return oss.str();
}

inline std::filesystem::path temp_path(const std::string& stem) {
  const auto dir = std::filesystem::temp_directory_path() / "vmp_runtime_timing_traps";
  std::filesystem::create_directories(dir);
  return dir / (stem + ".log");
}

inline std::vector<std::uint8_t> le64(std::uint64_t value) {
  std::vector<std::uint8_t> out(8u, 0u);
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = static_cast<std::uint8_t>((value >> (i * 8u)) & 0xffu);
  }
  return out;
}

inline bool contains_subsequence(const std::vector<std::uint8_t>& haystack, const std::vector<std::uint8_t>& needle) {
  if (needle.empty() || haystack.size() < needle.size()) {
    return false;
  }
  for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
    if (std::equal(needle.begin(), needle.end(), haystack.begin() + static_cast<std::ptrdiff_t>(i))) {
      return true;
    }
  }
  return false;
}

inline std::vector<std::uint64_t> absolute_counters(std::initializer_list<std::uint64_t> deltas,
                                                    std::uint64_t seed = 1000) {
  std::vector<std::uint64_t> out;
  out.reserve(deltas.size() + 1u);
  out.push_back(seed);
  auto current = seed;
  for (const auto delta : deltas) {
    current += delta;
    out.push_back(current);
  }
  return out;
}

inline vmp::runtime::obfuscation::TimingTrapProfile profile(std::uint32_t checkpoint_interval = 1u) {
  vmp::runtime::obfuscation::TimingTrapProfile out;
  out.checkpoint_interval = checkpoint_interval;
  out.min_cycles = 100u;
  out.max_cycles = 240u;
  out.median_cycles = 150u;
  out.p99_cycles = 200u;
  out.consecutive_anomaly_limit = 3u;
  return out;
}

inline vmp::runtime::vm1::Vm1Module load_vm1_roundtrip(const vmp::runtime::vm1::Vm1Module& module) {
  return vmp::runtime::vm1::Vm1Module::load_from_bytes(module.serialize());
}

inline vmp::runtime::vm2::Vm2Module load_vm2_roundtrip(const vmp::runtime::vm2::Vm2Module& module) {
  return vmp::runtime::vm2::Vm2Module::load_from_bytes(module.serialize());
}

}  // namespace vmp::tests::runtime_timing_traps
