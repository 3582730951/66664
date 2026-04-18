#include "test_common.h"

#include <iostream>

using namespace vmp::tests::runtime_vm1_jit;

int main() {
  auto audit_path = temp_path("jit_invalidate", ".log");
  vmp::runtime::audit::AuditWriter writer(audit_path);
  vmp::runtime::jit::Vm1Jit::instance().set_audit_writer(&writer);
  auto module = vmp::runtime::vm1::assemble_module_text(R"(
entry:
  ldi_u64 vr0, 7
  ret
)");
  require(run_module_reuse(module, "c", 2) == 7, "first JIT run failed");
  vmp::runtime::state::RuntimeState::instance().init_once(&writer, {"linux", "test", false});
  vmp::runtime::state::RuntimeState::instance().observe(vmp::runtime::state::RuntimeEventKind::key_rotated);
  require(vmp::runtime::jit::Vm1Jit::instance().module_entry_count(module.id()) == 0, "cache was not cleared");
  require(run_module_reuse(module, "c", 2) == 7, "second JIT run failed");
  writer.flush();
  const auto log = read_all(audit_path);
  require(log.find("jit_compile") != std::string::npos, "missing jit_compile audit");
  require(log.find("jit_invalidate") != std::string::npos, "missing jit_invalidate audit");
  std::cout << "jit_invalidate_key_rotation OK\n";
}
