#include <filesystem>
#include <iostream>
#include <string>

#include <vmp/runtime/audit/audit.h>
#include <vmp/runtime/audit/detector.h>
#include <vmp/runtime/audit/reaction.h>

namespace fs = std::filesystem;
namespace audit = vmp::runtime::audit;

int main(int argc, char** argv) {
  std::string mode;
  fs::path audit_path;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--mode" && i + 1 < argc) {
      mode = argv[++i];
    } else if (arg == "--audit-path" && i + 1 < argc) {
      audit_path = argv[++i];
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      return 2;
    }
  }
  if (mode.empty() || audit_path.empty()) {
    std::cerr << "usage: final_matrix_audit_helper --mode <emit|readonly> --audit-path <path>\n";
    return 2;
  }

  audit::AuditWriter writer(audit_path);
  audit::ReactionDispatcher dispatcher(writer, audit::ReactionPolicy::audit_only);
  audit::NullDetector detector;
  detector.set_sink([&](const audit::AnalysisEventRecord& record) { dispatcher.dispatch(record); });
  detector.start();

  if (mode == "emit") {
    detector.fire(audit::make_event("hw_breakpoint", "synthetic_hw", 0x1111, "vm_core", "entry_fn", 4, "x64", "linux", 10, 11,
                                    "2026-04-18", "10:00:00"));
    detector.fire(audit::make_event("integrity_mismatch", "synthetic_integrity", 0x2222, "vm_core", "checksum_guard", -8, "x64",
                                    "linux", 10, 12, "2026-04-18", "10:00:01"));
    detector.fire(audit::make_event("env_anomaly", "synthetic_env", 0x3333, "vm_core", "env_probe", 0, "x64", "linux", 10, 13,
                                    "2026-04-18", "10:00:02"));
    detector.fire(audit::make_event("unknown", "synthetic_unknown", 0, "", "", 0, "x64", "linux", 10, 14, "2026-04-18",
                                    "10:00:03"));
    writer.flush();
    std::cout << "emit_ok\n";
    return 0;
  }
  if (mode == "readonly") {
    detector.fire(audit::make_event("hw_breakpoint", "readonly_case", 0x4444, "vm_core", "entry_fn", 0, "x64", "linux", 20, 21,
                                    "2026-04-18", "10:00:04"));
    writer.flush();
    std::cout << "readonly_fallback_ok\n";
    return 0;
  }

  std::cerr << "unknown mode: " << mode << '\n';
  return 2;
}
