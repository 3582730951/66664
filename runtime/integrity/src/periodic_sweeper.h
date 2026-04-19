#pragma once

#include <optional>

#include <vmp/runtime/integrity/periodic_sweeper.h>

namespace vmp::runtime::integrity {

std::optional<std::uint32_t> parse_sweeper_interval_env(const char* raw) noexcept;

}  // namespace vmp::runtime::integrity
