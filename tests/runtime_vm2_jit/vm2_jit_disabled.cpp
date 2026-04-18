#include "test_common.h"

using namespace vmp::tests::runtime_vm2_jit;

int main() {
  auto audit_path = temp_path("vm2_jit_disabled", ".log");
  vmp::runtime::audit::AuditWriter writer(audit_path.string());
  vmp::runtime::audit::ReactionDispatcher dispatcher(writer, vmp::runtime::audit::ReactionPolicy::audit_only);
  auto module = assemble_text(fib20_program());
  auto result = run_module(module, "off", 40, {20}, nullptr, &dispatcher);
  writer.flush();
  require(result.ret_int == 6765, "disabled jit result mismatch");
  require(read_all(audit_path).find("vm2_jit_") == std::string::npos, "vm2_jit audit should be absent when disabled");
  std::cout << "vm2_jit_disabled OK\n";
}
