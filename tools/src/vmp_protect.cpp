#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include <vmp/policy/policy_ir.h>
#include <vmp/runtime/audit/audit.h>
#include <vmp/runtime/audit/detector.h>
#include <vmp/runtime/audit/reaction.h>

namespace {

struct Options {
  std::string policy_path;
  std::string emit_policy_json_path;
  bool dump_schema = false;
  bool validate_only = false;
  bool detector_selftest = false;
};

int usage(const char* argv0, const std::string& message = {}) {
  if (!message.empty()) {
    std::cerr << "error: " << message << '\n';
  }
  std::cerr << "usage: " << argv0
            << " [--dump-schema] [--policy <path>] [--emit-policy-json <path>] [--validate-only]"
            << " [--detector-selftest]" << std::endl;
  return 1;
}

Options parse_args(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--policy") {
      if (i + 1 >= argc) {
        throw std::runtime_error("--policy requires a path");
      }
      options.policy_path = argv[++i];
    } else if (arg == "--emit-policy-json") {
      if (i + 1 >= argc) {
        throw std::runtime_error("--emit-policy-json requires a path");
      }
      options.emit_policy_json_path = argv[++i];
    } else if (arg == "--dump-schema") {
      options.dump_schema = true;
    } else if (arg == "--validate-only") {
      options.validate_only = true;
    } else if (arg == "--detector-selftest") {
      options.detector_selftest = true;
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  return options;
}

int run_detector_selftest() {
  namespace audit = vmp::runtime::audit;
  audit::AuditWriter writer(audit::AuditWriter::default_path());
  audit::ReactionDispatcher dispatcher(writer, audit::ReactionPolicy::audit_then_delayed_exit);

  std::atomic<int> exit_calls{0};
  dispatcher.set_exit_fn([&exit_calls]() noexcept { exit_calls.fetch_add(1); });
  dispatcher.set_delay_selector([]() noexcept { return std::chrono::milliseconds(1000); });
  dispatcher.set_scheduler([](std::chrono::milliseconds, std::function<void()> fn) noexcept { fn(); });

  audit::NullDetector detector;
  detector.set_sink([&dispatcher](const audit::AnalysisEventRecord& record) { dispatcher.dispatch(record); });
  detector.start();

  detector.fire(audit::make_event("hw_breakpoint", "selftest_hw", 0x1111, "vm_core", "sensitive_entry", 4,
                                  "x64", "linux", 4242, 101, "2026-04-18", "10:00:00"));
  detector.fire(audit::make_event("integrity_mismatch", "selftest_integrity", 0x2222, "vm_core",
                                  "checksum_guard", -8, "x64", "linux", 4242, 102, "2026-04-18",
                                  "10:00:01"));
  detector.fire(audit::make_event("unknown", "selftest_unknown", 0, "", "", 0, "x64", "linux", 4242,
                                  103, "2026-04-18", "10:00:02"));

  writer.flush();
  std::cout << "audit:ok exits_triggered=" << exit_calls.load() << std::endl;
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = parse_args(argc, argv);

    if (options.detector_selftest) {
      return run_detector_selftest();
    }

    if (options.dump_schema) {
      vmp::policy::dump_schema(std::cout);
      if (options.policy_path.empty()) {
        return 0;
      }
    }

    if (options.policy_path.empty()) {
      std::cout << "NOT_IMPLEMENTED" << std::endl;
      return 0;
    }

    const auto policy_ir = vmp::policy::load_from_file(options.policy_path);
    const auto validation = vmp::policy::validate(policy_ir);

    bool has_error = false;
    for (const auto& item : validation) {
      std::ostream& out = item.is_error() ? std::cerr : std::cout;
      out << vmp::policy::format_validation_error(item) << '\n';
      has_error = has_error || item.is_error();
    }
    if (has_error) {
      return 2;
    }

    if (!options.emit_policy_json_path.empty()) {
      vmp::policy::save_to_file(policy_ir, options.emit_policy_json_path);
    }

    if (options.validate_only) {
      std::cout << "OK: policy loaded, " << policy_ir.entries.size() << " entries, schema=v"
                << policy_ir.schema_version << std::endl;
      return 0;
    }

    if (options.emit_policy_json_path.empty()) {
      std::cout << "OK: policy loaded, " << policy_ir.entries.size() << " entries, schema=v"
                << policy_ir.schema_version << std::endl;
      return 0;
    }

    return 0;
  } catch (const std::exception& ex) {
    return usage(argv[0], ex.what());
  }
}
