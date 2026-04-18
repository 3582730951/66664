#include <vmp/runtime/state/hot_recorder.h>

namespace vmp::runtime::state {

void HotRecorder::record_function_entry(std::uint64_t module_id, std::uint32_t pc) {
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.function_hits[{module_id, pc}]++;
}

void HotRecorder::record_block_entry(std::uint64_t module_id, std::uint32_t pc) {
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.block_hits[{module_id, pc}]++;
}

void HotRecorder::record_trace(std::uint64_t module_id, std::uint32_t from_pc, std::uint32_t to_pc) {
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.trace_edges[{module_id, from_pc, to_pc}]++;
}

void HotRecorder::record_jit_hit(std::uint64_t module_id, std::uint32_t pc) {
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.jit_hits[{module_id, pc}]++;
}

void HotRecorder::record_jit_miss(std::uint64_t module_id, std::uint32_t pc) {
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.jit_misses[{module_id, pc}]++;
}

void HotRecorder::record_domain_switch(std::string from, std::string to) {
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.domain_switches[{std::move(from), std::move(to)}]++;
}

void HotRecorder::record_sensitive_data_access(std::uint64_t module_id, std::uint32_t pc) {
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.sensitive_data_accesses[{module_id, pc}]++;
}

HotRecorderSnapshot HotRecorder::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_;
}

void HotRecorder::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_ = {};
}

void HotRecorder::set_uptime_seconds_for_tests(double uptime_seconds) {
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.uptime_seconds = uptime_seconds;
}

}  // namespace vmp::runtime::state
