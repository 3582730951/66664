#include <vmp/runtime/state/state.h>

#include <mutex>

#if VMP_WITH_JIT
#include <vmp/runtime/jit/vm1_jit.h>
#include <vmp/runtime/jit/vm2_jit.h>
#endif

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
#if VMP_WITH_JIT
  vmp::runtime::jit::Vm1Jit::instance().set_audit_writer(audit);
  vmp::runtime::jit::Vm2Jit::instance().set_audit_writer(audit);
#endif
  return true;
}

void RuntimeState::observe(RuntimeEventKind kind) noexcept {
  switch (kind) {
    case RuntimeEventKind::integrity_failed:
      set_flag(RuntimeFlag::integrity_failed);
#if VMP_WITH_JIT
      vmp::runtime::jit::Vm1Jit::instance().invalidate_all();
      vmp::runtime::jit::Vm2Jit::instance().invalidate_on_event(vmp::runtime::jit::Vm2JitEventKind::integrity_failed);
#endif
      break;
    case RuntimeEventKind::env_anomaly:
      set_flag(RuntimeFlag::env_anomaly);
#if VMP_WITH_JIT
      vmp::runtime::jit::Vm2Jit::instance().invalidate_on_event(vmp::runtime::jit::Vm2JitEventKind::detection_event);
#endif
      break;
    case RuntimeEventKind::key_rotated:
      set_flag(RuntimeFlag::key_rotated);
#if VMP_WITH_JIT
      vmp::runtime::jit::Vm1Jit::instance().invalidate_all();
      vmp::runtime::jit::Vm2Jit::instance().invalidate_on_event(vmp::runtime::jit::Vm2JitEventKind::key_rotated);
#endif
      break;
  }
}

void RuntimeState::detector_invalidate_module(std::uint64_t module_id) noexcept {
#if VMP_WITH_JIT
  vmp::runtime::jit::Vm1Jit::instance().invalidate_module(module_id);
  vmp::runtime::jit::Vm2Jit::instance().invalidate_module(module_id);
#else
  (void)module_id;
#endif
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

void RuntimeState::set_jit_capability(bool unavailable) noexcept {
  set_flag(RuntimeFlag::jit_execmem_unavailable, unavailable);
}

bool RuntimeState::check_flag(RuntimeFlag flag) const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return (flags_ & bit_for(flag)) != 0;
}

bool RuntimeState::jit_execmem_unavailable() const noexcept {
  return check_flag(RuntimeFlag::jit_execmem_unavailable);
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
#if VMP_WITH_JIT
  vmp::runtime::jit::Vm1Jit::instance().set_audit_writer(nullptr);
  vmp::runtime::jit::Vm2Jit::instance().set_audit_writer(nullptr);
#endif
}

std::uint32_t RuntimeState::bit_for(RuntimeFlag flag) noexcept {
  return static_cast<std::uint32_t>(flag);
}

const char* Facade::status() const noexcept { return "runtime_state_ready"; }

}  // namespace vmp::runtime::state
