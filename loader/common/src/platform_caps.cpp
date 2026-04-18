#include <vmp/loader/common/platform_caps.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace vmp::loader::common {
namespace {

constexpr const char* kAuditFilename = "vm_runtime_audit.log";

bool env_truthy(const char* name) noexcept {
  const char* value = std::getenv(name);
  return value != nullptr && *value != '\0';
}

std::filesystem::path env_path(const char* name) {
  if (const char* value = std::getenv(name); value != nullptr && *value != '\0') {
    return std::filesystem::path(value);
  }
  return {};
}

bool directory_writable(const std::filesystem::path& dir) noexcept {
  if (dir.empty()) {
    return false;
  }
  std::error_code ec;
  const auto existing = std::filesystem::exists(dir, ec);
  if (ec) {
    return false;
  }
  std::filesystem::path candidate = dir;
  if (!existing) {
    candidate = dir.parent_path();
    if (candidate.empty()) {
      candidate = ".";
    }
  }
#if defined(_WIN32)
  const auto native = candidate.wstring();
  return !native.empty() && ::GetFileAttributesW(native.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
  return ::access(candidate.c_str(), W_OK) == 0;
#endif
}

std::filesystem::path cwd_path() noexcept {
  try {
    return std::filesystem::current_path();
  } catch (...) {
    return {};
  }
}

std::filesystem::path temp_dir() noexcept {
  try {
    return std::filesystem::temp_directory_path();
  } catch (...) {
#if defined(_WIN32)
    return cwd_path();
#else
    return std::filesystem::path("/tmp");
#endif
  }
}

std::filesystem::path append_log_name(std::filesystem::path base) {
  if (base.empty()) {
    return base;
  }
  if (base.has_filename() && base.extension() == ".log") {
    return base;
  }
  return base / kAuditFilename;
}

std::filesystem::path android_default_dir() noexcept {
  if (auto files_dir = env_path("VMP_ANDROID_FILES_DIR"); !files_dir.empty() && directory_writable(files_dir)) {
    return files_dir;
  }
  if (const char* pkg = std::getenv("VMP_ANDROID_PACKAGE"); pkg != nullptr && *pkg != '\0') {
    auto pkg_dir = std::filesystem::path("/data/data") / pkg / "files";
    if (directory_writable(pkg_dir)) {
      return pkg_dir;
    }
  }
  auto cwd = cwd_path();
  if (directory_writable(cwd)) {
    return cwd;
  }
  return temp_dir();
}

std::filesystem::path ios_default_dir() noexcept {
  if (auto docs = env_path("VMP_IOS_DOCUMENTS_DIR"); !docs.empty() && directory_writable(docs)) {
    return docs;
  }
  if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    auto docs = std::filesystem::path(home) / "Documents";
    if (directory_writable(docs)) {
      return docs;
    }
  }
  auto cwd = cwd_path();
  if (directory_writable(cwd)) {
    return cwd;
  }
  return temp_dir();
}

std::filesystem::path windows_default_dir() noexcept {
  if (auto local = env_path("LOCALAPPDATA"); !local.empty() && directory_writable(local)) {
    return local / "vmp";
  }
  auto cwd = cwd_path();
  if (directory_writable(cwd)) {
    return cwd;
  }
  return temp_dir();
}

std::filesystem::path posix_default_dir() noexcept {
  auto cwd = cwd_path();
  if (directory_writable(cwd)) {
    return cwd;
  }
  return temp_dir();
}

}  // namespace

bool detect_execmem_available() noexcept {
  if (const char* force = std::getenv("VMP_FORCE_JIT_CAPABILITY"); force != nullptr && std::string_view(force) == "disallow") {
    return false;
  }
#if defined(_WIN32)
  void* region = ::VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (region == nullptr) {
    return false;
  }
  DWORD old_protect = 0;
  const bool ok = ::VirtualProtect(region, 16, PAGE_EXECUTE_READ, &old_protect) != 0;
  ::VirtualFree(region, 0, MEM_RELEASE);
  return ok;
#else
  void* region = ::mmap(nullptr, 16, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (region == MAP_FAILED) {
    return false;
  }
  const bool ok = ::mprotect(region, 16, PROT_READ | PROT_EXEC) == 0;
  ::munmap(region, 16);
  return ok;
#endif
}

std::filesystem::path detect_default_audit_path(std::string_view platform) noexcept {
  if (auto override_path = env_path("VMP_AUDIT_PATH"); !override_path.empty()) {
    return override_path;
  }
  if (auto legacy_override = env_path("VMP_AUDIT_LOG_PATH"); !legacy_override.empty()) {
    return legacy_override;
  }

  if (platform == "android") {
    return append_log_name(android_default_dir());
  }
  if (platform == "ios") {
    return append_log_name(ios_default_dir());
  }
  if (platform == "windows") {
    return append_log_name(windows_default_dir());
  }
  return append_log_name(posix_default_dir());
}

}  // namespace vmp::loader::common
