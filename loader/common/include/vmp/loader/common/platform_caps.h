#pragma once

#include <filesystem>
#include <string_view>

namespace vmp::loader::common {

bool detect_execmem_available() noexcept;
std::filesystem::path detect_default_audit_path(std::string_view platform) noexcept;

}  // namespace vmp::loader::common
