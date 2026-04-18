#include "test_common.h"

#include <iostream>

using namespace vmp::tests::runtime_vm1;

int main() {
  try {
    require_int(run_text(R"(
entry:
  ldi_u64 vr0, 4
  ldi_u64 vr1, 6
  call @add2, 2
  call @double_it, 1
  ret
add2:
  add vr0, vr0, vr1
  ret
double_it:
  add vr0, vr0, vr0
  ret
    )").ret_int, 20, "nested_call");

    require_int(run_text(R"(
entry:
  ldi_u64 vr0, 1
  ldi_u64 vr1, 2
  ldi_u64 vr2, 3
  ldi_u64 vr3, 4
  ldi_u64 vr4, 5
  ldi_u64 vr5, 6
  ldi_u64 vr6, 7
  ldi_u64 vr7, 8
  ldi_u64 vr8, 9
  ldi_u64 vr9, 10
  call @sum10, 10
  ret
sum10:
  add vr0, vr0, vr1
  add vr0, vr0, vr2
  add vr0, vr0, vr3
  add vr0, vr0, vr4
  add vr0, vr0, vr5
  add vr0, vr0, vr6
  add vr0, vr0, vr7
  add vr0, vr0, vr8
  add vr0, vr0, vr9
  ret
    )").ret_int, 55, "register_and_stack_args");

    std::cout << "vm1_call_ret OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vm1_call_ret failed: " << ex.what() << '\n';
    return 1;
  }
}
