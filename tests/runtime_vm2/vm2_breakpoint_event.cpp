#include "test_common.h"

#include <filesystem>
#include <iostream>

using namespace vmp::tests::runtime_vm2;
namespace audit = vmp::runtime::audit;

int main() {
  try {
    const auto path = std::filesystem::temp_directory_path() / "vm2_breakpoint_event.log";
    audit::AuditWriter writer(path);
    audit::ReactionDispatcher dispatcher(writer, audit::ReactionPolicy::audit_only);
    require_int(run_text("brk\nildimm r0, 7\nbret\n", {}, nullptr, &dispatcher).ret_int, 7, "result");
    writer.flush();
    const auto content = read_file(path);
    require(content.find("vm2_breakpoint") != std::string::npos, "missing vm2_breakpoint audit line");
    std::cout << "vm2_breakpoint_event OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vm2_breakpoint_event failed: " << ex.what() << '\n';
    return 1;
  }
}
