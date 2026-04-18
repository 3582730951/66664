#include "test_common.h"

#include <iostream>

using namespace vmp::tests::runtime_vm1_jit;

int main() {
  auto audit_path = temp_path("jit_disabled", ".log");
  vmp::runtime::audit::AuditWriter writer(audit_path);
  vmp::runtime::jit::Vm1Jit::instance().set_audit_writer(&writer);
  auto module = vmp::runtime::vm1::assemble_module_text(R"(
entry:
  ldi_u64 vr0, 123
  ret
)");
  require(run_module_reuse(module, "off", 2) == 123, "interpreter result mismatch");
  writer.flush();
  require(read_all(audit_path).find("jit_") == std::string::npos, "jit audit should be empty when disabled");
  std::cout << "jit_disabled OK\n";
}
