#pragma once

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <vmp/runtime/audit/audit.h>
#include <vmp/runtime/audit/reaction.h>
#include <vmp/runtime/bridge/bridge.h>
#include <vmp/runtime/vm2/vm2.h>

namespace vmp::tests::runtime_vm2 {

inline void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

inline std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("failed to open " + path.string());
  std::ostringstream oss;
  oss << input.rdbuf();
  return oss.str();
}

inline vmp::runtime::vm2::Vm2Module assemble_text(const std::string& text) {
  return vmp::runtime::vm2::assemble_module_text(text);
}

inline vmp::runtime::vm2::ExecutionResult run_text(
    const std::string& text,
    const std::vector<std::uint64_t>& int_args = {},
    vmp::runtime::bridge::BridgeRegistry* bridge = nullptr,
    vmp::runtime::audit::ReactionDispatcher* dispatcher = nullptr,
    int max_depth = 64,
    std::shared_ptr<vmp::runtime::strings::StringPool> string_pool = nullptr,
    std::shared_ptr<vmp::runtime::strings::KeyContext> key_context = nullptr) {
  auto module = assemble_text(text);
  vmp::runtime::vm2::Vm2Context context(module);
  for (std::size_t i = 0; i < int_args.size() && i < 8; ++i) context.r[i] = int_args[i];
  const std::size_t spill_count = int_args.size() > 8 ? int_args.size() - 8 : 0u;
  if (spill_count > 0) {
    const auto spill_base = context.allocate_spill(spill_count * sizeof(std::uint64_t), "test spill");
    for (std::size_t i = 0; i < spill_count; ++i) {
      context.write_memory<std::uint64_t>(spill_base + static_cast<std::uint64_t>(i * sizeof(std::uint64_t)), int_args[8 + i]);
    }
  }
  context.bridge_registry = bridge;
  context.audit_dispatcher = dispatcher;
  context.max_bridge_depth = max_depth;
  context.string_pool = std::move(string_pool);
  context.key_context = std::move(key_context);
  vmp::runtime::vm2::Vm2Interpreter interpreter;
  return interpreter.execute(context);
}

inline void require_int(std::uint64_t actual, std::uint64_t expected, const std::string& label) {
  if (actual != expected) {
    throw std::runtime_error(label + ": expected=" + std::to_string(expected) + " actual=" + std::to_string(actual));
  }
}

inline void require_double(double actual, double expected, const std::string& label) {
  if (std::fabs(actual - expected) > 1e-9) {
    throw std::runtime_error(label + ": expected=" + std::to_string(expected) + " actual=" + std::to_string(actual));
  }
}

inline void require_vec(vmp::runtime::vm2::Vec128 actual, std::uint64_t lo, std::uint64_t hi, const std::string& label) {
  if (actual.u64.lo != lo || actual.u64.hi != hi) {
    std::ostringstream oss;
    oss << label << ": expected=(" << lo << ',' << hi << ") actual=(" << actual.u64.lo << ',' << actual.u64.hi << ')';
    throw std::runtime_error(oss.str());
  }
}

}  // namespace vmp::tests::runtime_vm2
