#pragma once

#include <cstdint>
#include <optional>

namespace vmp::runtime::integrity {

class RegionRegistry;

class PeriodicSweeper {
 public:
  PeriodicSweeper() = default;
  ~PeriodicSweeper();

  PeriodicSweeper(const PeriodicSweeper&) = delete;
  PeriodicSweeper& operator=(const PeriodicSweeper&) = delete;

  void start(std::uint32_t interval_ms, RegionRegistry& registry);
  void stop();

 private:
  class Impl;
  Impl* impl_ = nullptr;
};

std::optional<std::uint32_t> sweeper_interval_from_env() noexcept;
PeriodicSweeper& global_periodic_sweeper() noexcept;
void maybe_start_periodic_sweeper_from_env(RegionRegistry& registry);
void stop_global_periodic_sweeper() noexcept;

}  // namespace vmp::runtime::integrity
