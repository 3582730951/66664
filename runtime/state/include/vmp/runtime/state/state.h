#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include <vmp/runtime/audit/audit.h>
#include <vmp/runtime/state/hot_recorder.h>
#include <vmp/runtime/state/profile.h>

namespace vmp::runtime::state {

enum class RuntimeFlag : std::uint32_t {
  loader_initialized = 1u << 0,
  integrity_failed = 1u << 1,
  env_anomaly = 1u << 2,
  key_rotated = 1u << 3,
  key_context_loaded = 1u << 4,
  placeholder_called = 1u << 5,
  jit_execmem_unavailable = 1u << 6,
};

enum class RuntimeEventKind {
  integrity_failed,
  env_anomaly,
  key_rotated,
};

struct RuntimeConfig {
  std::string platform;
  std::string loader_entrypoint;
  bool loader_disabled = false;
};

class RuntimeState {
 public:
  static RuntimeState& instance() noexcept;

  bool init_once(vmp::runtime::audit::AuditWriter* audit, RuntimeConfig config) noexcept;
  void observe(RuntimeEventKind kind) noexcept;
  void detector_invalidate_module(std::uint64_t module_id) noexcept;
  void set_flag(RuntimeFlag flag, bool enabled = true) noexcept;
  void set_jit_capability(bool jit_execmem_unavailable) noexcept;
  bool check_flag(RuntimeFlag flag) const noexcept;
  bool jit_execmem_unavailable() const noexcept;
  vmp::runtime::audit::AuditWriter* get_audit() const noexcept;
  RuntimeConfig config() const;
  bool load_offline_profile(const std::string& path) noexcept;
  OfflineProfile offline_profile() const;
  OfflineProfile fused_profile_snapshot() const;
  HotRecorder& hot_recorder() noexcept;
  const HotRecorder& hot_recorder() const noexcept;
  double online_weight() const noexcept;
  void append_audit_event(const std::string& event_type,
                          const std::string& context_note,
                          std::uint64_t program_counter = 0) const noexcept;
  void shutdown() noexcept;

 private:
  RuntimeState() = default;

  static std::uint32_t bit_for(RuntimeFlag flag) noexcept;

  mutable std::mutex mutex_;
  vmp::runtime::audit::AuditWriter* audit_ = nullptr;
  RuntimeConfig config_{};
  OfflineProfile offline_profile_{};
  HotRecorder hot_recorder_{};
  double online_weight_ = 0.4;
  std::uint32_t flags_ = 0;
  bool initialized_ = false;
};

struct Facade {
  const char* status() const noexcept;
};

}  // namespace vmp::runtime::state
