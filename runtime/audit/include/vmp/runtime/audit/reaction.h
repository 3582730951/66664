#pragma once

#include <chrono>
#include <functional>

#include <vmp/runtime/audit/audit.h>

namespace vmp::runtime::audit {

enum class ReactionPolicy { log, degrade, decoy_terminate, audit_only, audit_then_delayed_exit };

struct ReactionDispatcher {
  explicit ReactionDispatcher(AuditWriter& writer, ReactionPolicy default_policy);

  void dispatch(const AnalysisEventRecord& record) noexcept;
  void dispatch(const AnalysisEventRecord& record, ReactionPolicy policy) noexcept;

  void set_exit_fn(std::function<void()> exit_fn) noexcept;
  void set_scheduler(std::function<void(std::chrono::milliseconds, std::function<void()>)> scheduler) noexcept;
  void set_delay_selector(std::function<std::chrono::milliseconds()> selector) noexcept;

 private:
  AuditWriter& writer;
  ReactionPolicy default_policy;
  std::function<void()> exit_fn;
  std::function<void(std::chrono::milliseconds, std::function<void()>)> scheduler;
  std::function<std::chrono::milliseconds()> delay_selector;
};

}  // namespace vmp::runtime::audit
