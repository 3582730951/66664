#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include <vmp/policy/policy_ir.h>
#include <vmp/runtime/audit/audit.h>
#include <vmp/runtime/audit/detector.h>
#include <vmp/runtime/audit/reaction.h>

#include "string_protect_common.h"

namespace {

struct Options {
  std::string policy_path;
  std::string emit_policy_json_path;
  bool dump_schema = false;
  bool validate_only = false;
  bool detector_selftest = false;
  bool protect_strings = false;
  std::string string_bin = "string_pool.bin";
  std::string string_idx = "string_pool.idx.json";
  std::string string_kdf = "key_derivation.json";
};

int usage(const char* argv0, const std::string& message = {}) {
  if (!message.empty()) {
    std::cerr << "error: " << message << '\n';
  }
  std::cerr << "usage: " << argv0
            << " [--dump-schema] [--policy <path>] [--emit-policy-json <path>] [--validate-only]"
            << " [--detector-selftest] [--protect-strings --string-bin <bin> --string-idx <idx> --string-kdf <kdf>]"
            << std::endl;
  return 1;
}

Options parse_args(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--policy") {
      options.policy_path = argv[++i];
    } else if (arg == "--emit-policy-json") {
      options.emit_policy_json_path = argv[++i];
    } else if (arg == "--dump-schema") {
      options.dump_schema = true;
    } else if (arg == "--validate-only") {
      options.validate_only = true;
    } else if (arg == "--detector-selftest") {
      options.detector_selftest = true;
    } else if (arg == "--protect-strings") {
      options.protect_strings = true;
    } else if (arg == "--string-bin") {
      options.string_bin = argv[++i];
    } else if (arg == "--string-idx") {
      options.string_idx = argv[++i];
    } else if (arg == "--string-kdf") {
      options.string_kdf = argv[++i];
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
      return usage(argv[0], "--policy is required unless --dump-schema or --detector-selftest is used");
    }

    auto raw_policy = vmp::tools::strings_tool::json::parse(vmp::tools::strings_tool::read_text(options.policy_path));
    if (raw_policy.contains("entries") && raw_policy["entries"].is_array()) {
      for (auto& entry : raw_policy["entries"]) {
        if (entry.is_object()) {
          entry.erase("string_id");
          entry.erase("value");
        }
      }
    }
    const auto policy_ir = vmp::policy::load_from_string(raw_policy.dump());
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

    if (options.validate_only && !options.protect_strings) {
      std::cout << "OK: policy loaded, " << policy_ir.entries.size() << " entries, schema=v"
                << policy_ir.schema_version << std::endl;
      return 0;
    }

    std::size_t protected_count = 0;
    if (options.protect_strings) {
      auto master_key = vmp::tools::strings_tool::resolve_master_key();
      const auto outputs = vmp::tools::strings_tool::protect_policy_strings(options.policy_path, options.string_bin,
                                                                            options.string_idx, options.string_kdf,
                                                                            master_key);
      vmp::runtime::strings::secure_memzero(master_key.data(), master_key.size());
      protected_count = outputs.protected_count;
    }

    std::cout << "OK: policy loaded, " << policy_ir.entries.size() << " entries, schema=v" << policy_ir.schema_version;
    if (options.protect_strings) {
      std::cout << " strings:protected=" << protected_count;
    }
    std::cout << std::endl;
    return 0;
  } catch (const std::exception& ex) {
    return usage(argv[0], ex.what());
  }
}
