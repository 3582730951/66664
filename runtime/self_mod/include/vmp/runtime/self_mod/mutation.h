#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <vmp/runtime/cryptor/rolling_policy.h>
#include <vmp/runtime/self_mod/interlock.h>

namespace vmp::runtime::vm1 {
class Vm1Context;
class Vm1Module;
}

namespace vmp::runtime::vm2 {
class Vm2Context;
class Vm2Module;
}

namespace vmp::runtime::self_mod {

struct MutationRule {
  std::uint32_t trigger_pc = 0;
  std::uint32_t target_pc = 0;
  std::uint32_t length = 0;
  std::array<std::uint8_t, 32> base_key{};
  std::array<std::uint8_t, 32> expected_state_hash{};
  std::array<std::uint8_t, 32> expected_hmac{};
};

struct ModuleConfig {
  std::vector<MutationRule> mutations;
  std::vector<InterlockRule> interlocks;
};

struct TrailerSplit {
  std::size_t payload_end = 0;
  std::vector<std::uint8_t> trailer;
};

struct MutationObservation {
  vmp::runtime::cryptor::VmDomain domain = vmp::runtime::cryptor::VmDomain::vm1;
  std::uint64_t module_id = 0;
  std::uint32_t trigger_pc = 0;
  std::uint32_t target_pc = 0;
  std::vector<std::uint8_t> original_bytes;
  std::vector<std::uint8_t> mutated_bytes;
};

struct FetchObservation {
  vmp::runtime::cryptor::VmDomain domain = vmp::runtime::cryptor::VmDomain::vm1;
  std::uint64_t module_id = 0;
  std::uint32_t pc = 0;
  std::uint8_t static_byte = 0;
  std::uint8_t runtime_byte = 0;
  bool mutated = false;
};

struct ObserverHooks {
  std::function<void(const MutationObservation&)> on_mutation;
  std::function<void(const FetchObservation&)> on_fetch;
};

class ScopedObserverHooks {
 public:
  explicit ScopedObserverHooks(ObserverHooks hooks);
  ~ScopedObserverHooks();

  ScopedObserverHooks(const ScopedObserverHooks&) = delete;
  ScopedObserverHooks& operator=(const ScopedObserverHooks&) = delete;

 private:
  bool active_ = false;
};

class ScopedExecution {
 public:
  explicit ScopedExecution(vmp::runtime::vm1::Vm1Context& context);
  explicit ScopedExecution(vmp::runtime::vm2::Vm2Context& context);
  ~ScopedExecution();

  ScopedExecution(const ScopedExecution&) = delete;
  ScopedExecution& operator=(const ScopedExecution&) = delete;

 private:
  bool active_ = false;
};

MutationRule make_vm1_mutation_rule(const vmp::runtime::vm1::Vm1Context& context,
                                    std::uint32_t trigger_pc,
                                    std::uint32_t target_pc,
                                    std::uint32_t length,
                                    const std::array<std::uint8_t, 32>& base_key);

MutationRule make_vm2_mutation_rule(const vmp::runtime::vm2::Vm2Context& context,
                                    std::uint32_t trigger_pc,
                                    std::uint32_t target_pc,
                                    std::uint32_t length,
                                    const std::array<std::uint8_t, 32>& base_key);

void attach(vmp::runtime::vm1::Vm1Module& module, const ModuleConfig& config);
void attach(vmp::runtime::vm2::Vm2Module& module, const ModuleConfig& config);

std::optional<ModuleConfig> decode(const vmp::runtime::vm1::Vm1Module& module);
std::optional<ModuleConfig> decode(const vmp::runtime::vm2::Vm2Module& module);

TrailerSplit split_serialized_metadata(const std::vector<std::uint8_t>& bytes);

using ByteProvider = std::function<std::uint8_t(std::size_t forward_pc)>;

std::uint8_t fetch_vm1_byte(const vmp::runtime::vm1::Vm1Module& module,
                            std::size_t forward_pc,
                            const ByteProvider& base_provider);

std::uint8_t fetch_vm2_byte(const vmp::runtime::vm2::Vm2Module& module,
                            std::size_t forward_pc,
                            const ByteProvider& base_provider);

}  // namespace vmp::runtime::self_mod
