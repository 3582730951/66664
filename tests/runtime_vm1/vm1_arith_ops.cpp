#include "test_common.h"

#include <iostream>

using namespace vmp::tests::runtime_vm1;

int main() {
  try {
    require_int(run_text("ldi_u64 vr0, 7\nldi_u64 vr1, 5\nadd vr0, vr0, vr1\nret\n").ret_int, 12, "add");
    require_int(run_text("ldi_u64 vr0, 7\nldi_u64 vr1, 5\nsub vr0, vr0, vr1\nret\n").ret_int, 2, "sub");
    require_int(run_text("ldi_u64 vr0, 7\nldi_u64 vr1, 5\nmul vr0, vr0, vr1\nret\n").ret_int, 35, "mul");
    require_int(run_text("ldi_u64 vr0, 21\nldi_u64 vr1, 3\ndiv vr0, vr0, vr1\nret\n").ret_int, 7, "div");
    require_int(run_text("ldi_u64 vr0, 22\nldi_u64 vr1, 5\nmod vr0, vr0, vr1\nret\n").ret_int, 2, "mod");
    require_int(run_text("ldi_u64 vr0, 6\nldi_u64 vr1, 3\nand vr0, vr0, vr1\nret\n").ret_int, 2, "and");
    require_int(run_text("ldi_u64 vr0, 6\nldi_u64 vr1, 3\nor vr0, vr0, vr1\nret\n").ret_int, 7, "or");
    require_int(run_text("ldi_u64 vr0, 6\nldi_u64 vr1, 3\nxor vr0, vr0, vr1\nret\n").ret_int, 5, "xor");
    require_int(run_text("ldi_u64 vr0, 3\nldi_u64 vr1, 4\nshl vr0, vr0, vr1\nret\n").ret_int, 48, "shl");
    require_int(run_text("ldi_u64 vr0, 48\nldi_u64 vr1, 4\nshr vr0, vr0, vr1\nret\n").ret_int, 3, "shr");
    require_int(run_text("ldi64 vr0, -16\nldi_u64 vr1, 2\nsar vr0, vr0, vr1\nret\n").ret_int, static_cast<std::uint64_t>(-4), "sar");
    require_int(run_text("ldi_u64 vr0, 5\nneg vr0, vr0\nret\n").ret_int, static_cast<std::uint64_t>(-5), "neg");
    require_int(run_text("ldi_u64 vr0, 0\nnot vr0, vr0\nret\n").ret_int, ~0ULL, "not");
    require_double(run_text("ldi_f64 vfr0, 3.25\ndomain_ret\n").ret_float, 3.25, "ldi_f64/domain_ret");
    require_int(run_text(R"(
      ldi_u64 vr1, 123
      mov vr0, vr1
      ret
    )").ret_int, 123, "mov");
    require_int(run_text(R"(
      ldi_u64 vr1, 0
      ldi_u64 vr2, 99
      store_mem64 [sp+0], vr2
      load_mem64 vr0, [sp+0]
      ret
    )").ret_int, 99, "load_store64");
    require_int(run_text(R"(
      ldi_u64 vr1, 0x1122334455667788
      store_mem8 [sp+0], vr1
      store_mem16 [sp+8], vr1
      store_mem32 [sp+16], vr1
      store_mem64 [sp+24], vr1
      load_mem8 vr2, [sp+0]
      load_mem16 vr3, [sp+8]
      load_mem32 vr4, [sp+16]
      load_mem64 vr5, [sp+24]
      add vr0, vr2, vr3
      add vr0, vr0, vr4
      add vr0, vr0, vr5
      ret
    )").ret_int, 0x55667788u + 0x7788u + 0x88u + 0x1122334455667788ULL, "load_store_widths");
    std::cout << "vm1_arith_ops OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vm1_arith_ops failed: " << ex.what() << '\n';
    return 1;
  }
}
