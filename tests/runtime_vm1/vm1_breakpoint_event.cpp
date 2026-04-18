#include "test_common.h"

#include <iostream>

using namespace vmp::tests::runtime_vm1;
namespace audit = vmp::runtime::audit;

int main() {
  try {
    const auto dir = std::filesystem::temp_directory_path() / "vmp_vm1_breakpoint_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto log = dir / "audit.log";
    audit::AuditWriter writer(log);
    audit::ReactionDispatcher dispatcher(writer, audit::ReactionPolicy::audit_only);
    (void)run_text("breakpoint\nldi_u64 vr0, 1\nret\n", {}, nullptr, &dispatcher);
    writer.flush();
    const auto content = read_file(log);
    require(content.find("vm1_breakpoint") != std::string::npos, "missing vm1_breakpoint audit line");
    std::cout << "vm1_breakpoint_event OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vm1_breakpoint_event failed: " << ex.what() << '\n';
    return 1;
  }
}
