#include <vmp/runtime/audit/audit.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <string_view>
#include <thread>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <utility>

#if defined(_WIN32)
#include <process.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace vmp::runtime::audit {
namespace {

std::tm local_tm(std::time_t now) {
  std::tm tm_value{};
#if defined(_WIN32)
  localtime_s(&tm_value, &now);
#else
  localtime_r(&now, &tm_value);
#endif
  return tm_value;
}

std::string format_tm(const char* pattern) {
  const auto now = std::time(nullptr);
  const auto tm_value = local_tm(now);
  char buffer[32]{};
  if (std::strftime(buffer, sizeof(buffer), pattern, &tm_value) == 0) {
    return {};
  }
  return buffer;
}

std::uint64_t current_process_id() noexcept {
#if defined(_WIN32)
  return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
  return static_cast<std::uint64_t>(getpid());
#endif
}

std::uint64_t current_thread_id() noexcept {
#if defined(_WIN32)
  return static_cast<std::uint64_t>(GetCurrentThreadId());
#elif defined(SYS_gettid)
  return static_cast<std::uint64_t>(syscall(SYS_gettid));
#else
  return static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

std::string current_arch() {
#ifdef VMP_ARCH_STR
  return VMP_ARCH_STR;
#else
  return "unknown";
#endif
}

std::string current_platform() {
#if defined(__ANDROID__)
  return "android";
#elif defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "ios";
#elif defined(__linux__)
  return "linux";
#elif defined(VMP_PLATFORM_STR)
  return VMP_PLATFORM_STR;
#else
  return "unknown";
#endif
}

std::string escape_note(std::string_view note) {
  std::string out;
  out.reserve(note.size());
  for (char ch : note) {
    switch (ch) {
      case ']':
        out += "\\x5D";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        break;
      default:
        out.push_back(ch);
        break;
    }
  }
  return out;
}

std::string format_offset(std::int64_t offset) {
  std::ostringstream oss;
  const char sign = offset < 0 ? '-' : '+';
  const auto magnitude = offset < 0 ? static_cast<std::uint64_t>(-(offset + 1)) + 1
                                    : static_cast<std::uint64_t>(offset);
  oss << sign << "0x" << std::hex << std::nouppercase << magnitude;
  return oss.str();
}

void ensure_parent_directory(const std::filesystem::path& path) noexcept {
  try {
    const auto parent = path.parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent);
    }
  } catch (...) {
  }
}

}  // namespace

struct AuditWriter::Impl {
  explicit Impl(std::filesystem::path input_path) : path(std::move(input_path)) {
    ensure_parent_directory(path);
#if defined(_WIN32)
    handle = CreateFileW(path.wstring().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      handle = nullptr;
    }
#else
    fd = ::open(path.c_str(), O_CREAT | O_APPEND | O_WRONLY | O_CLOEXEC, 0644);
    if (fd < 0) {
      fd = -1;
    }
#endif
  }

  ~Impl() {
#if defined(_WIN32)
    if (handle != nullptr) {
      CloseHandle(handle);
      handle = nullptr;
    }
#else
    if (fd >= 0) {
      ::close(fd);
      fd = -1;
    }
#endif
  }

  std::filesystem::path path;
  std::mutex mutex;
#if defined(_WIN32)
  HANDLE handle = nullptr;
#else
  int fd = -1;
#endif
};

AnalysisEventRecord make_event(std::string event_type,
                               std::string context_note,
                               std::uint64_t program_counter,
                               std::string module_name,
                               std::string symbol_name,
                               std::int64_t symbol_offset,
                               std::string arch,
                               std::string platform,
                               std::uint64_t process_id,
                               std::uint64_t thread_id,
                               std::string event_date,
                               std::string event_time) {
  AnalysisEventRecord record;
  record.event_type = std::move(event_type);
  record.context_note = std::move(context_note);
  record.program_counter = program_counter;
  record.module_name = std::move(module_name);
  record.symbol_name = std::move(symbol_name);
  record.symbol_offset = symbol_offset;
  record.arch = arch.empty() ? current_arch() : std::move(arch);
  record.platform = platform.empty() ? current_platform() : std::move(platform);
  record.process_id = process_id == 0 ? current_process_id() : process_id;
  record.thread_id = thread_id == 0 ? current_thread_id() : thread_id;
  record.event_date = event_date.empty() ? format_tm("%Y-%m-%d") : std::move(event_date);
  record.event_time = event_time.empty() ? format_tm("%H:%M:%S") : std::move(event_time);
  return record;
}

std::string format_line(const AnalysisEventRecord& record) {
  std::ostringstream oss;
  oss << '[' << record.event_date << "] [" << record.event_time << "] [" << record.platform << "] ["
      << record.arch << "] [pid=" << record.process_id << "] [tid=" << record.thread_id << "] ["
      << record.event_type << "] [pc=0x" << std::hex << std::nouppercase << record.program_counter << std::dec
      << "] [module=" << record.module_name << "] [symbol=" << record.symbol_name << "] [offset="
      << format_offset(record.symbol_offset) << "] [note=" << escape_note(record.context_note) << ']';
  return oss.str();
}

AuditWriter::AuditWriter(std::filesystem::path log_path) : impl_(new Impl(std::move(log_path))) {}

AuditWriter::~AuditWriter() { delete impl_; }

void AuditWriter::append(const AnalysisEventRecord& record) noexcept {
  if (impl_ == nullptr) {
    return;
  }
  const auto line = format_line(record) + "\n";
  std::lock_guard<std::mutex> lock(impl_->mutex);
  try {
#if defined(_WIN32)
    if (impl_->handle == nullptr) {
      return;
    }
    DWORD written = 0;
    WriteFile(impl_->handle, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
#else
    if (impl_->fd < 0) {
      return;
    }
    const auto* data = line.data();
    std::size_t remaining = line.size();
    while (remaining > 0) {
      const auto rv = ::write(impl_->fd, data, remaining);
      if (rv <= 0) {
        break;
      }
      remaining -= static_cast<std::size_t>(rv);
      data += rv;
    }
#endif
  } catch (...) {
  }
}

void AuditWriter::flush() noexcept {
  if (impl_ == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
#if defined(_WIN32)
  if (impl_->handle != nullptr) {
    FlushFileBuffers(impl_->handle);
  }
#else
  if (impl_->fd >= 0) {
    ::fsync(impl_->fd);
  }
#endif
}

std::filesystem::path AuditWriter::default_path() {
  if (const char* override_path = std::getenv("VMP_AUDIT_LOG_PATH"); override_path != nullptr && *override_path != '\0') {
    return std::filesystem::path(override_path);
  }
#if defined(_WIN32)
  if (const char* local_app_data = std::getenv("LOCALAPPDATA"); local_app_data != nullptr && *local_app_data != '\0') {
    return std::filesystem::path(local_app_data) / "vmp" / "vm_runtime_audit.log";
  }
  return std::filesystem::current_path() / "vm_runtime_audit.log";
#elif defined(__ANDROID__)
  if (const char* files_dir = std::getenv("VMP_ANDROID_FILES_DIR"); files_dir != nullptr && *files_dir != '\0') {
    return std::filesystem::path(files_dir) / "vm_runtime_audit.log";
  }
  return std::filesystem::current_path() / "vm_runtime_audit.log";
#elif defined(__APPLE__)
  if (const char* documents_dir = std::getenv("VMP_IOS_DOCUMENTS_DIR"); documents_dir != nullptr && *documents_dir != '\0') {
    return std::filesystem::path(documents_dir) / "vm_runtime_audit.log";
  }
  return std::filesystem::current_path() / "vm_runtime_audit.log";
#else
  return std::filesystem::current_path() / "vm_runtime_audit.log";
#endif
}

}  // namespace vmp::runtime::audit
