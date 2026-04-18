#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include <vmp/runtime/audit/audit.h>

namespace vmp::runtime::state {

enum class RuntimeFlag : std::uint32_t {
  loader_initialized = 1u << 0,
  integrity_failed = 1u << 1,
  env_anomaly = 1u << 2,
  key_rotated = 1u << 3,
  key_context_loaded = 1u << 4,
  placeholder_called = 1u << 5,
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
  void set_flag(RuntimeFlag flag, bool enabled = true) noexcept;
  bool check_flag(RuntimeFlag flag) const noexcept;
  vmp::runtime::audit::AuditWriter* get_audit() const noexcept;
  RuntimeConfig config() const;
  void shutdown() noexcept;

 private:
  RuntimeState() = default;

  static std::uint32_t bit_for(RuntimeFlag flag) noexcept;

  mutable std::mutex mutex_;
  vmp::runtime::audit::AuditWriter* audit_ = nullptr;
  RuntimeConfig config_{};
  std::uint32_t flags_ = 0;
  bool initialized_ = false;
};

struct Facade {
  const char* status() const noexcept;
};

}  // namespace vmp::runtime::state
