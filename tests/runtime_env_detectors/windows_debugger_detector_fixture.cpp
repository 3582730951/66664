#include <vmp/runtime/audit/audit.h>
#include <vmp/runtime/audit/reaction.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

#include <windows.h>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: windows_debugger_detector_fixture <audit-log>\n";
    return 2;
  }

  vmp::runtime::audit::AuditWriter writer{std::filesystem::path(argv[1])};
  vmp::runtime::audit::ReactionDispatcher dispatcher(writer, vmp::runtime::audit::ReactionPolicy::audit_only);

  BOOL remote_debugger = FALSE;
  const BOOL is_debugger_present = IsDebuggerPresent();
  const BOOL remote_query_ok = CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote_debugger);
  const bool detected = is_debugger_present != FALSE || (remote_query_ok != FALSE && remote_debugger != FALSE);

  if (detected) {
    std::string note = "IsDebuggerPresent=" + std::to_string(is_debugger_present != FALSE) +
                       " CheckRemoteDebuggerPresent.ok=" + std::to_string(remote_query_ok != FALSE) +
                       " CheckRemoteDebuggerPresent.detected=" + std::to_string(remote_debugger != FALSE);
    dispatcher.dispatch(vmp::runtime::audit::make_event("debugger_detected",
                                                        std::move(note),
                                                        0,
                                                        "windows_debugger_detector_fixture",
                                                        "debug_api"));
    writer.flush();
  }

  std::cout << "windows_debugger_detector_fixture detected=" << (detected ? "true" : "false") << "\n";
  return detected ? 0 : 3;
}
