#include <vmp/runtime/integrity/periodic_sweeper.h>

#include "periodic_sweeper.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <utility>

#include <vmp/runtime/integrity/region.h>

namespace vmp::runtime::integrity {
namespace {
using namespace std::chrono_literals;

class PeriodicSweeperImpl {
 public:
  ~PeriodicSweeperImpl() { stop(); }

  void start(std::uint32_t interval_ms, RegionRegistry& registry) {
    if (interval_ms < 50u) {
      throw std::invalid_argument("periodic sweeper interval must be >= 50ms");
    }
    stop();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      interval_ = std::chrono::milliseconds(interval_ms);
      registry_ = &registry;
      running_ = true;
    }
    thread_ = std::thread([this]() { run(); });
  }

  void stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      running_ = false;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    registry_ = nullptr;
    interval_ = 0ms;
  }

 private:
  void run() noexcept {
    while (true) {
      RegionRegistry* registry = nullptr;
      std::chrono::milliseconds interval{0};
      {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!running_) {
          break;
        }
        interval = interval_;
        cv_.wait_for(lock, interval, [this]() { return !running_; });
        if (!running_) {
          break;
        }
        registry = registry_;
      }
      if (registry == nullptr) {
        continue;
      }
      try {
        const auto fast_results = registry->verify_all(RegionRegistry::Mode::fast);
        for (const auto& result : fast_results) {
          if (result.status == RegionVerifyStatus::mismatch) {
            (void)registry->verify_one(result.name, RegionRegistry::Mode::authoritative);
          }
        }
      } catch (...) {
      }
    }
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  bool running_ = false;
  std::chrono::milliseconds interval_{0};
  RegionRegistry* registry_ = nullptr;
  std::thread thread_;
};

}  // namespace

class PeriodicSweeper::Impl : public PeriodicSweeperImpl {};

PeriodicSweeper::~PeriodicSweeper() {
  if (impl_ != nullptr) {
    impl_->stop();
    delete impl_;
    impl_ = nullptr;
  }
}

void PeriodicSweeper::start(std::uint32_t interval_ms, RegionRegistry& registry) {
  if (impl_ == nullptr) {
    impl_ = new Impl();
  }
  impl_->start(interval_ms, registry);
}

void PeriodicSweeper::stop() {
  if (impl_ != nullptr) {
    impl_->stop();
  }
}

std::optional<std::uint32_t> parse_sweeper_interval_env(const char* raw) noexcept {
  if (raw == nullptr || *raw == '\0') {
    return std::nullopt;
  }
  try {
    const auto value = static_cast<std::uint32_t>(std::stoul(raw));
    if (value < 50u) {
      return std::nullopt;
    }
    return value;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::uint32_t> sweeper_interval_from_env() noexcept {
  return parse_sweeper_interval_env(std::getenv("VMP_INTEGRITY_SWEEP_MS"));
}

PeriodicSweeper& global_periodic_sweeper() noexcept {
  static PeriodicSweeper sweeper;
  return sweeper;
}

void maybe_start_periodic_sweeper_from_env(RegionRegistry& registry) {
  const auto interval = sweeper_interval_from_env();
  if (!interval.has_value()) {
    return;
  }
  global_periodic_sweeper().start(*interval, registry);
}

void stop_global_periodic_sweeper() noexcept { global_periodic_sweeper().stop(); }

}  // namespace vmp::runtime::integrity
