#include "test_common.h"

#include <iostream>

using namespace vmp::tests::runtime_vm1;

int main() {
  try {
    require_int(run_text(R"(
      ldi_u64 vr0, 0
      ldi_u64 vr1, 0
      ldi_u64 vr2, 10
loop:
      add vr1, vr1, vr0
      ldi_u64 vr3, 1
      add vr0, vr0, vr3
      jlt vr0, vr2, @loop
      mov vr0, vr1
      ret
    )").ret_int, 45, "loop_sum");

    require_int(run_text(R"(
      ldi_u64 vr0, 5
      ldi_u64 vr1, 7
      jeq vr0, vr1, @bad
      jne vr0, vr1, @good
bad:
      ldi_u64 vr0, 0
      ret
good:
      jgt vr1, vr0, @gt
      ldi_u64 vr0, 0
      ret
gt:
      jge vr1, vr0, @ge
      ldi_u64 vr0, 0
      ret
ge:
      jle vr0, vr1, @ok
      ldi_u64 vr0, 0
      ret
ok:
      ldi_u64 vr0, 1
      ret
    )").ret_int, 1, "conditions");

    require_int(run_text(R"(
entry:
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
    )", {20}).ret_int, 6765, "fib_recursive");

    std::cout << "vm1_control_flow OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vm1_control_flow failed: " << ex.what() << '\n';
    return 1;
  }
}
