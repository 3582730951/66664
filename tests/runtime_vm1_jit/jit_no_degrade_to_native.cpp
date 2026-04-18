#include "test_common.h"

#include <iostream>

using namespace vmp::tests::runtime_vm1_jit;

int main() {
  auto module = vmp::runtime::vm1::assemble_module_text(R"(
entry:
  ldi_u64 vr0, 88
  ret
)");
  require(run_module_reuse(module, "c", 3) == 88, "vm1 entry result mismatch");
  const auto stats = vmp::runtime::jit::Vm1Jit::instance().entry_stats(module.id(), module.entry_pc);
  require(stats.entry_trampoline_hits > 0, "expected vm1 JIT entry trampoline hits");
  std::cout << "jit_no_degrade_to_native OK\n";
}
