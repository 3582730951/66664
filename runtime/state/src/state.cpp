#include <vmp/runtime/state/state.h>

#include <mutex>

namespace vmp::runtime::state {

RuntimeState& RuntimeState::instance() noexcept {
  static RuntimeState state;
  return state;
}

bool RuntimeState::init_once(vmp::runtime::audit::AuditWriter* audit, RuntimeConfig config) noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  if (initialized_) {
    return false;
  }
  audit_ = audit;
  config_ = std::move(config);
  initialized_ = true;
  flags_ |= bit_for(RuntimeFlag::loader_initialized);
  return true;
}

void RuntimeState::observe(RuntimeEventKind kind) noexcept {
  switch (kind) {
    case RuntimeEventKind::integrity_failed:
      set_flag(RuntimeFlag::integrity_failed);
      break;
    case RuntimeEventKind::env_anomaly:
      set_flag(RuntimeFlag::env_anomaly);
      break;
    case RuntimeEventKind::key_rotated:
      set_flag(RuntimeFlag::key_rotated);
      break;
  }
}

void RuntimeState::set_flag(RuntimeFlag flag, bool enabled) noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto bit = bit_for(flag);
  if (enabled) {
    flags_ |= bit;
  } else {
    flags_ &= ~bit;
  }
}

bool RuntimeState::check_flag(RuntimeFlag flag) const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return (flags_ & bit_for(flag)) != 0;
}

vmp::runtime::audit::AuditWriter* RuntimeState::get_audit() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return audit_;
}

RuntimeConfig RuntimeState::config() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return config_;
}

void RuntimeState::shutdown() noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  audit_ = nullptr;
  config_ = RuntimeConfig{};
  flags_ = 0;
  initialized_ = false;
}

std::uint32_t RuntimeState::bit_for(RuntimeFlag flag) noexcept {
  return static_cast<std::uint32_t>(flag);
}

const char* Facade::status() const noexcept { return "runtime_state_ready"; }

}  // namespace vmp::runtime::state
