#include "test_common.h"

#include <iostream>

using namespace vmp::tests::runtime_vm2;

int main() {
  try {
    require_int(run_text(R"(
  ildimm r0, 0
  ildimm r1, 4
loop:
  iadd r0, r0, r1
  ildimm r2, 1
  isub r1, r1, r2
  jp p1, @done
  jmp @loop
done:
  bret
)").ret_int, 10, "nested_loop_sum");

    require_int(run_text(R"(
  blnk @fib, 1
  bret
fib:
  ildimm r10, 2
  isub r11, r0, r10
  jp p1, @base
  imov r20, r0
  ildimm r10, 1
  isub r0, r20, r10
  blnk @fib, 1
  imov r21, r0
  ildimm r10, 2
  isub r0, r20, r10
  blnk @fib, 1
  iadd r0, r21, r0
  bret
base:
  bret
)", {20}).ret_int, 6765, "fib_recursive");

    require_int(run_text(R"(
  ildimm r0, 1
  ildimm r1, 1
  isub r2, r0, r1
  pcall p0, @same, 0
  bret
same:
  ildimm r0, 7
  bret
)").ret_int, 7, "pcall_predicate");

    std::cout << "vm2_control_flow OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vm2_control_flow failed: " << ex.what() << '\n';
    return 1;
  }
}
