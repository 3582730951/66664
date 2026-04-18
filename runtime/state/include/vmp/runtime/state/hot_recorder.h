#pragma once

#include <mutex>
#include <string>

#include <vmp/runtime/state/profile.h>

namespace vmp::runtime::state {

class HotRecorder {
 public:
  HotRecorder() = default;

  void record_function_entry(std::uint64_t module_id, std::uint32_t pc);
  void record_block_entry(std::uint64_t module_id, std::uint32_t pc);
  void record_trace(std::uint64_t module_id, std::uint32_t from_pc, std::uint32_t to_pc);
  void record_jit_hit(std::uint64_t module_id, std::uint32_t pc);
  void record_jit_miss(std::uint64_t module_id, std::uint32_t pc);
  void record_domain_switch(std::string from, std::string to);
  void record_sensitive_data_access(std::uint64_t module_id, std::uint32_t pc);

  HotRecorderSnapshot snapshot() const;
  void reset();
  void set_uptime_seconds_for_tests(double uptime_seconds);

 private:
  mutable std::mutex mutex_;
  HotRecorderSnapshot snapshot_;
};

}  // namespace vmp::runtime::state
