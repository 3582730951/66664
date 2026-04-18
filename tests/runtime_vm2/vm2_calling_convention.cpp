#include "test_common.h"

#include <iostream>

using namespace vmp::tests::runtime_vm2;

int main() {
  try {
    require_int(run_text(R"(
  blnk @sum10, 10
  bret
sum10:
  ildimm r20, 0
  iadd r20, r20, r0
  iadd r20, r20, r1
  iadd r20, r20, r2
  iadd r20, r20, r3
  iadd r20, r20, r4
  iadd r20, r20, r5
  iadd r20, r20, r6
  iadd r20, r20, r7
  imemld64 r21, [sp+0]
  iadd r20, r20, r21
  imemld64 r22, [sp+8]
  iadd r20, r20, r22
  imov r0, r20
  bret
)", {1,2,3,4,5,6,7,8,9,10}).ret_int, 55, "overflow_args");

    const auto vec = run_text(R"(
.vconst c0, 0xaa, 0xbb
  blnk @mkvec, 0
  bret
mkvec:
  vldimm q0, c0
  bret
)").ret_vec;
    require_vec(vec, 0xaa, 0xbb, "vector_return");

    std::cout << "vm2_calling_convention OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vm2_calling_convention failed: " << ex.what() << '\n';
    return 1;
  }
}
