#include "test_common.h"

#include <iostream>

using namespace vmp::tests::runtime_vm1_jit;

int main() {
  auto module = vmp::runtime::vm1::assemble_module_text(R"(
entry:
  ldi_u64 vr0, 20
  call @fib, 1
  ret
fib:
  ldi_u64 vr1, 2
  jlt vr0, vr1, @base
  mov vr2, vr0
  ldi_u64 vr3, 1
  sub vr0, vr2, vr3
  call @fib, 1
  mov vr4, vr0
  ldi_u64 vr3, 2
  sub vr0, vr2, vr3
  call @fib, 1
  add vr0, vr0, vr4
  ret
base:
  ret
)");
  const auto interp = run_module_reuse(module, "off", 1);
  const auto jit = run_module_reuse(module, "c", 3);
  require(interp == jit && jit == 6765, "fib(20) mismatch");
  std::cout << "jit_correctness_fib_recursive OK\n";
}
