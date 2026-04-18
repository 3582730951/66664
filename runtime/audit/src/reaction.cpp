#include <vmp/runtime/audit/reaction.h>

#include <random>
#include <thread>

namespace vmp::runtime::audit {
namespace {

std::chrono::milliseconds default_delay() noexcept {
  try {
    thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<int> distribution(1000, 3000);
    return std::chrono::milliseconds(distribution(rng));
  } catch (...) {
    return std::chrono::milliseconds(1000);
  }
}

void default_exit() noexcept { std::quick_exit(0); }

void default_scheduler(std::chrono::milliseconds delay, std::function<void()> hook) noexcept {
  try {
    std::thread([delay, hook = std::move(hook)]() mutable {
      std::this_thread::sleep_for(delay);
      try {
        hook();
      } catch (...) {
      }
    }).detach();
  } catch (...) {
    try {
      hook();
    } catch (...) {
    }
  }
}

}  // namespace

ReactionDispatcher::ReactionDispatcher(AuditWriter& writer_in, ReactionPolicy default_policy_in)
    : writer(writer_in),
      default_policy(default_policy_in),
      exit_fn(default_exit),
      scheduler(default_scheduler),
      delay_selector(default_delay) {}

void ReactionDispatcher::dispatch(const AnalysisEventRecord& record) noexcept { dispatch(record, default_policy); }

void ReactionDispatcher::dispatch(const AnalysisEventRecord& record, ReactionPolicy policy) noexcept {
  try {
    switch (policy) {
      case ReactionPolicy::audit_only:
        writer.append(record);
        break;
      case ReactionPolicy::audit_then_delayed_exit: {
        writer.append(record);
        auto delay = delay_selector ? delay_selector() : std::chrono::milliseconds(1000);
        auto exit_copy = exit_fn;
        if (scheduler) {
          scheduler(delay, [exit_copy]() mutable {
            if (exit_copy) {
              exit_copy();
            }
          });
        }
        break;
      }
      case ReactionPolicy::log:
        // TODO(subtask_04): integrate log-state-machine behavior.
        break;
      case ReactionPolicy::degrade:
        // TODO(subtask_04): integrate degrade-state-machine behavior.
        break;
      case ReactionPolicy::decoy_terminate:
        // TODO(subtask_04): integrate decoy-terminate-state-machine behavior.
        break;
    }
  } catch (...) {
  }
}

void ReactionDispatcher::set_exit_fn(std::function<void()> exit_fn_in) noexcept { exit_fn = std::move(exit_fn_in); }

void ReactionDispatcher::set_scheduler(
    std::function<void(std::chrono::milliseconds, std::function<void()>)> scheduler_in) noexcept {
  scheduler = std::move(scheduler_in);
}

void ReactionDispatcher::set_delay_selector(std::function<std::chrono::milliseconds()> selector) noexcept {
  delay_selector = std::move(selector);
}

}  // namespace vmp::runtime::audit
