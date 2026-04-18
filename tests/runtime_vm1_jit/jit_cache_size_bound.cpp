#include "test_common.h"

#include <iostream>
#include <sstream>

using namespace vmp::tests::runtime_vm1_jit;

int main() {
  auto audit_path = temp_path("jit_cache", ".log");
  vmp::runtime::audit::AuditWriter writer(audit_path);
  vmp::runtime::jit::Vm1Jit::instance().set_audit_writer(&writer);
  EnvGuard budget_guard("VMP_JIT_CACHE_BUDGET", "128");
  auto asm_text = std::ostringstream{};
  asm_text << "entry:\n";
  for (int i = 0; i < 10; ++i) {
    asm_text << "  ldi_u64 vr0, " << i << "\n";
    if (i != 9) {
      asm_text << "  jmp @b" << (i + 1) << "\n";
      asm_text << "b" << (i + 1) << ":\n";
    }
  }
  asm_text << "  ret\n";
  auto module = vmp::runtime::vm1::assemble_module_text(asm_text.str());
  require(run_module_reuse(module, "c", 2) == 9, "cache bound module returned wrong result");
  writer.flush();
  require(vmp::runtime::jit::Vm1Jit::instance().module_cache_bytes(module.id()) <= 128, "cache budget exceeded");
  const auto log = read_all(audit_path);
  require(log.find("jit_oom") != std::string::npos, "expected jit_oom eviction audit");
  std::cout << "jit_cache_size_bound OK\n";
}
