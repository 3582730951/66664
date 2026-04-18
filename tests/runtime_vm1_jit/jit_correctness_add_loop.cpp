#include "test_common.h"

#include <iostream>

using namespace vmp::tests::runtime_vm1_jit;

int main() {
  auto module = vmp::runtime::vm1::assemble_module_text(R"(
entry:
  ldi_u64 vr0, 0
  ldi_u64 vr1, 1
  ldi_u64 vr2, 10001
  ldi_u64 vr3, 1
loop:
  add vr0, vr0, vr1
  add vr1, vr1, vr3
  jlt vr1, vr2, @loop
  ret
)");
  const auto interp = run_module_reuse(module, "off", 1);
  const auto jit = run_module_reuse(module, "c", 3);
  require(interp == jit, "jit add loop mismatch");
  require(jit == 50005000ull, "unexpected sum result");
  std::cout << "jit_correctness_add_loop OK\n";
}
