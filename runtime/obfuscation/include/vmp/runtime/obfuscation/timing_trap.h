#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <vmp/runtime/audit/reaction.h>

namespace vmp::runtime::vm1 {
class Vm1Module;
}

namespace vmp::runtime::vm2 {
class Vm2Module;
}

namespace vmp::runtime::obfuscation {

struct TimingTrapProfile {
  std::uint32_t checkpoint_interval = 0;
  std::uint64_t min_cycles = 0;
  std::uint64_t max_cycles = 0;
  std::uint64_t median_cycles = 0;
  std::uint64_t p99_cycles = 0;
  std::uint32_t consecutive_anomaly_limit = 3;

  bool operator==(const TimingTrapProfile& other) const noexcept;
};

enum class TimingTrapAnomaly : std::uint8_t {
  none = 0,
  too_fast = 1,
  too_slow = 2,
};

struct TimingTrapObservation {
  bool siren_active = false;
  bool triggered_now = false;
  TimingTrapAnomaly anomaly = TimingTrapAnomaly::none;
  std::uint64_t delta_cycles = 0;
  std::uint64_t primary_bias = 0;
  std::uint64_t secondary_bias = 0;
  std::uint32_t checkpoint_pc = 0;
};

struct TimingTrapRuntimeState;

struct TimingTrapTrailerSplit {
  std::size_t payload_end = 0;
  std::vector<std::uint8_t> trailer;
};

class ScopedTimingTrapCounterOverride {
 public:
  explicit ScopedTimingTrapCounterOverride(std::function<std::uint64_t()> provider) noexcept;
  ~ScopedTimingTrapCounterOverride();

  ScopedTimingTrapCounterOverride(const ScopedTimingTrapCounterOverride&) = delete;
  ScopedTimingTrapCounterOverride& operator=(const ScopedTimingTrapCounterOverride&) = delete;

 private:
  std::function<std::uint64_t()> previous_;
};

void attach_timing_trap_profile(vmp::runtime::vm1::Vm1Module& module, const TimingTrapProfile& profile);
void attach_timing_trap_profile(vmp::runtime::vm2::Vm2Module& module, const TimingTrapProfile& profile);
std::optional<TimingTrapProfile> decode_timing_trap_profile(const vmp::runtime::vm1::Vm1Module& module);
std::optional<TimingTrapProfile> decode_timing_trap_profile(const vmp::runtime::vm2::Vm2Module& module);
std::shared_ptr<TimingTrapRuntimeState> make_timing_trap_state(const vmp::runtime::vm1::Vm1Module& module);
std::shared_ptr<TimingTrapRuntimeState> make_timing_trap_state(const vmp::runtime::vm2::Vm2Module& module);
TimingTrapObservation observe_timing_checkpoint(TimingTrapRuntimeState& state,
                                                std::uint32_t checkpoint_pc,
                                                std::uint64_t module_id,
                                                vmp::runtime::audit::ReactionDispatcher* dispatcher,
                                                std::string_view domain) noexcept;
TimingTrapTrailerSplit split_serialized_timing_trap_metadata(const std::vector<std::uint8_t>& bytes);

}  // namespace vmp::runtime::obfuscation
