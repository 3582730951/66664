#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <vmp/runtime/audit/reaction.h>
#include <vmp/runtime/self_mod/interlock.h>
#include <vmp/runtime/self_mod/mutation.h>
#include <vmp/runtime/vm1/vm1.h>
#include <vmp/runtime/vm2/vm2.h>

namespace vmp::tests::runtime_self_mod {

inline void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

inline std::filesystem::path temp_path(const std::string& stem, const std::string& ext = ".log") {
  const auto dir = std::filesystem::temp_directory_path() / "vmp_runtime_self_mod";
  std::filesystem::create_directories(dir);
  return dir / (stem + ext);
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

inline std::array<std::uint8_t, 32> bytes32(std::uint8_t seed) {
  std::array<std::uint8_t, 32> out{};
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(i));
  }
  return out;
}

inline std::array<std::uint8_t, 16> bytes16(std::uint8_t seed) {
  std::array<std::uint8_t, 16> out{};
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(i));
  }
  return out;
}

inline vmp::runtime::audit::ReactionDispatcher make_dispatcher(vmp::runtime::audit::AuditWriter& writer,
                                                               bool& exit_requested) {
  vmp::runtime::audit::ReactionDispatcher dispatcher(writer, vmp::runtime::audit::ReactionPolicy::audit_only);
  dispatcher.set_delay_selector([] { return std::chrono::milliseconds(0); });
  dispatcher.set_scheduler([](std::chrono::milliseconds, std::function<void()> hook) {
    if (hook) {
      hook();
    }
  });
  dispatcher.set_exit_fn([&exit_requested] { exit_requested = true; });
  return dispatcher;
}

}  // namespace vmp::tests::runtime_self_mod
