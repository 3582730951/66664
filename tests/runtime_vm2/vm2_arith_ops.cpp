#include "test_common.h"

#include <iostream>

using namespace vmp::tests::runtime_vm2;

int main() {
  try {
    const auto result = run_text(R"(
.vconst c0, 0x0102030405060708, 0x1112131415161718
.vconst c1, 0x1, 0x2
  ildimm r0, 84
  ildimm r1, 21
  idiv r2, r0, r1
  ildimm r3, 5
  imod r4, r0, r3
  iadd r5, r2, r4
  ixor r6, r5, r1
  vldimm q0, c0
  vldimm q1, c1
  vxor128 q0, q0, q1
  imov r0, r6
  bret
)");
    require_int(result.ret_int, ((84 / 21) + (84 % 5)) ^ 21, "int_arith");
    require_vec(result.ret_vec, 0x0102030405060709ULL, 0x111213141516171AULL, "vector_xor");
    std::cout << "vm2_arith_ops OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vm2_arith_ops failed: " << ex.what() << '\n';
    return 1;
  }
}
