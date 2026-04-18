#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include <vmp/runtime/audit/audit.h>
#include <vmp/runtime/audit/detector.h>
#include <vmp/runtime/audit/placeholder.h>
#include <vmp/runtime/audit/reaction.h>

namespace {
using json = nlohmann::json;
namespace audit = vmp::runtime::audit;

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string slurp(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to open " + path.string());
  }
  std::ostringstream oss;
  oss << input.rdbuf();
  return oss.str();
}

std::vector<std::string> read_lines(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    lines.push_back(line);
  }
  return lines;
}

std::filesystem::path temp_dir(const std::string& name) {
  const auto dir = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

json read_json(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to open json " + path.string());
  }
  json parsed;
  input >> parsed;
  return parsed;
}

audit::AnalysisEventRecord event_from_json(const json& data) {
  return audit::AnalysisEventRecord{
      data.at("event_date").get<std::string>(),
      data.at("event_time").get<std::string>(),
      data.at("thread_id").get<std::uint64_t>(),
      data.at("event_type").get<std::string>(),
      data.at("program_counter").get<std::uint64_t>(),
      data.at("module_name").get<std::string>(),
      data.at("symbol_name").get<std::string>(),
      data.at("symbol_offset").get<std::int64_t>(),
      data.at("arch").get<std::string>(),
      data.at("platform").get<std::string>(),
      data.at("process_id").get<std::uint64_t>(),
      data.at("context_note").get<std::string>(),
  };
}

void test_line_format_exact() {
  const auto record = event_from_json(read_json("/workspace/vmp/tests/audit/golden_record.json"));
  const auto expected = slurp("/workspace/vmp/tests/audit/golden_line.txt");
  require(audit::format_line(record) + "\n" == expected, "formatted line mismatch");
}

void test_fail_to_open() {
  audit::AuditWriter writer("/proc/self/status/audit.log");
  writer.append(audit::make_event("unknown", "still_running", 0, "", "", 0, "x64", "linux", 111, 222,
                                  "2026-04-18", "01:02:03"));
  writer.flush();
}

void test_reaction_audit_only() {
  const auto dir = temp_dir("vmp_audit_only_cpp");
  const auto log = dir / "audit.log";
  audit::AuditWriter writer(log);
  audit::ReactionDispatcher dispatcher(writer, audit::ReactionPolicy::audit_only);
  int exit_calls = 0;
  dispatcher.set_exit_fn([&exit_calls]() noexcept { ++exit_calls; });
  dispatcher.set_delay_selector([]() noexcept { return std::chrono::milliseconds(1000); });
  dispatcher.set_scheduler([](std::chrono::milliseconds, std::function<void()> fn) noexcept { fn(); });
  dispatcher.dispatch(audit::make_event("integrity_mismatch", "audit only", 0x55, "mod", "sym", -4, "x64",
                                        "linux", 77, 88, "2026-04-18", "08:09:10"));
  writer.flush();
  const auto lines = read_lines(log);
  require(lines.size() == 1, "audit_only should write one line");
  require(exit_calls == 0, "audit_only should not trigger exit");
}

void test_reaction_audit_then_delayed_exit() {
  const auto dir = temp_dir("vmp_audit_then_exit_cpp");
  const auto log = dir / "audit.log";
  audit::AuditWriter writer(log);
  audit::ReactionDispatcher dispatcher(writer, audit::ReactionPolicy::audit_only);
  int exit_calls = 0;
  bool scheduler_called = false;
  std::chrono::milliseconds observed_delay{0};
  dispatcher.set_exit_fn([&exit_calls]() noexcept { ++exit_calls; });
  dispatcher.set_delay_selector([]() noexcept { return std::chrono::milliseconds(2000); });
  dispatcher.set_scheduler([&](std::chrono::milliseconds delay, std::function<void()> fn) noexcept {
    scheduler_called = true;
    observed_delay = delay;
    fn();
  });
  auto record = audit::make_event("hw_breakpoint", "owner override", 0x99, "core", "sensitive", 8, "x64",
                                  "linux", 90, 91, "2026-04-18", "11:22:33");
  dispatcher.dispatch(record, audit::ReactionPolicy::audit_then_delayed_exit);
  writer.flush();
  const auto lines = read_lines(log);
  require(lines.size() == 1, "audit_then_delayed_exit should write one line");
  require(exit_calls == 1, "audit_then_delayed_exit should trigger exit once");
  require(scheduler_called, "scheduler hook should be called");
  require(observed_delay == std::chrono::milliseconds(2000), "delay hook mismatch");
}

void test_concurrency() {
  const auto dir = temp_dir("vmp_audit_concurrency_cpp");
  const auto log = dir / "audit.log";
  audit::AuditWriter writer(log);
  constexpr int kThreads = 4;
  constexpr int kPerThread = 10000;
  std::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back([&, t]() {
      for (int i = 0; i < kPerThread; ++i) {
        writer.append(audit::make_event("unknown", "thread=" + std::to_string(t), static_cast<std::uint64_t>(i),
                                        "", "", 0, "x64", "linux", 500, static_cast<std::uint64_t>(t),
                                        "2026-04-18", "12:00:00"));
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }
  writer.flush();
  const auto lines = read_lines(log);
  require(lines.size() == static_cast<std::size_t>(kThreads * kPerThread), "unexpected line count");
  for (const auto& line : lines) {
    require(line.find('\n') == std::string::npos, "line corruption: embedded newline");
    require(!line.empty() && line.front() == '[', "line corruption: malformed prefix");
  }
}

void test_null_detector() {
  audit::NullDetector detector;
  std::vector<audit::AnalysisEventRecord> received;
  detector.set_sink([&](const audit::AnalysisEventRecord& record) { received.push_back(record); });
  detector.start();
  detector.fire(audit::make_event("unknown", "first", 1, "", "", 0, "x64", "linux", 1, 2,
                                  "2026-04-18", "00:00:01"));
  detector.fire(audit::make_event("hw_breakpoint", "second", 2, "", "", 0, "x64", "linux", 1, 2,
                                  "2026-04-18", "00:00:02"));
  require(received.size() == 2, "NullDetector should forward events after start");
  require(detector.name() == "null", "NullDetector name mismatch");
}

void test_placeholder_init() {
  audit::initialize_placeholder_hook_once();
  audit::initialize_placeholder_hook_once();
  vm_placeholder_analysis_awareness_hook();
}

}  // namespace

int main() {
  try {
    test_line_format_exact();
    test_fail_to_open();
    test_reaction_audit_only();
    test_reaction_audit_then_delayed_exit();
    test_concurrency();
    test_null_detector();
    test_placeholder_init();
    std::cout << "audit_cpp_test OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "audit_cpp_test failed: " << ex.what() << '\n';
    return 1;
  }
}
