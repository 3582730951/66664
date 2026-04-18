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
#include <vmp/runtime/vm1/vm1.h>

namespace vmp::tests::runtime_vm1 {

inline void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

inline std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to open " + path.string());
  }
  std::ostringstream oss;
  oss << input.rdbuf();
  return oss.str();
}

inline vmp::runtime::vm1::Vm1Module assemble_text(const std::string& text) {
  return vmp::runtime::vm1::assemble_module_text(text);
}

inline vmp::runtime::vm1::ExecutionResult run_text(
    const std::string& text,
    const std::vector<std::uint64_t>& int_args = {},
    vmp::runtime::bridge::BridgeRegistry* bridge = nullptr,
    vmp::runtime::audit::ReactionDispatcher* dispatcher = nullptr,
    int max_depth = 64) {
  auto module = assemble_text(text);
  vmp::runtime::vm1::Vm1Context context(module);
  for (std::size_t i = 0; i < int_args.size() && i < 8; ++i) {
    context.vr[i] = int_args[i];
  }
  context.bridge_registry = bridge;
  context.audit_dispatcher = dispatcher;
  context.max_bridge_depth = max_depth;
  vmp::runtime::vm1::Vm1Interpreter interpreter;
  return interpreter.execute(context);
}

inline void require_int(std::uint64_t actual, std::uint64_t expected, const std::string& label) {
  if (actual != expected) {
    throw std::runtime_error(label + ": expected=" + std::to_string(expected) + " actual=" +
                             std::to_string(actual));
  }
}

inline void require_double(double actual, double expected, const std::string& label) {
  if (std::fabs(actual - expected) > 1e-9) {
    throw std::runtime_error(label + ": expected=" + std::to_string(expected) + " actual=" +
                             std::to_string(actual));
  }
}

}  // namespace vmp::tests::runtime_vm1
