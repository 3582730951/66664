#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace vmp::runtime::audit {

struct AnalysisEventRecord {
  std::string event_date;
  std::string event_time;
  std::uint64_t thread_id = 0;
  std::string event_type;
  std::uint64_t program_counter = 0;
  std::string module_name;
  std::string symbol_name;
  std::int64_t symbol_offset = 0;
  std::string arch;
  std::string platform;
  std::uint64_t process_id = 0;
  std::string context_note;
};

AnalysisEventRecord make_event(std::string event_type,
                               std::string context_note = {},
                               std::uint64_t program_counter = 0,
                               std::string module_name = {},
                               std::string symbol_name = {},
                               std::int64_t symbol_offset = 0,
                               std::string arch = {},
                               std::string platform = {},
                               std::uint64_t process_id = 0,
                               std::uint64_t thread_id = 0,
                               std::string event_date = {},
                               std::string event_time = {});

std::string format_line(const AnalysisEventRecord& record);

class AuditWriter {
 public:
  explicit AuditWriter(std::filesystem::path log_path);
  ~AuditWriter();

  AuditWriter(const AuditWriter&) = delete;
  AuditWriter& operator=(const AuditWriter&) = delete;
  AuditWriter(AuditWriter&&) = delete;
  AuditWriter& operator=(AuditWriter&&) = delete;

  void append(const AnalysisEventRecord& record) noexcept;
  void flush() noexcept;

  static std::filesystem::path default_path();

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};

}  // namespace vmp::runtime::audit
