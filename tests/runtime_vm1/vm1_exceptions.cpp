#include "test_common.h"

#include <iostream>

using namespace vmp::tests::runtime_vm1;
namespace vm1 = vmp::runtime::vm1;

int main() {
  try {
    bool divide_by_zero = false;
    try {
      (void)run_text("ldi_u64 vr0, 1\nldi_u64 vr1, 0\ndiv vr0, vr0, vr1\nret\n");
    } catch (const vm1::VmException& ex) {
      divide_by_zero = ex.code() == vm1::VmTrapCode::divide_by_zero;
    }
    require(divide_by_zero, "divide_by_zero trap expected");

    bool out_of_bounds = false;
    try {
      (void)run_text("ldi_u64 vr1, 70000\nload_mem64 vr0, [vr1+0]\nret\n");
    } catch (const vm1::VmException& ex) {
      out_of_bounds = ex.code() == vm1::VmTrapCode::out_of_bounds;
    }
    require(out_of_bounds, "out_of_bounds trap expected");

    bool trap_seen = false;
    try {
      (void)run_text("trap 7\n");
    } catch (const vm1::VmException& ex) {
      trap_seen = ex.code() == vm1::VmTrapCode::trap_instruction;
    }
    require(trap_seen, "trap instruction expected");

    bool unknown_seen = false;
    try {
      auto module = vm1::assemble_module_text("nop\nret\n");
      auto bytes = module.serialize();
      bytes[16] = 0xFE;
      auto bad = vm1::Vm1Module::load_from_bytes(bytes);
      vm1::Vm1Context ctx(bad);
      vm1::Vm1Interpreter interpreter;
      (void)interpreter.execute(ctx);
    } catch (const vm1::VmException& ex) {
      unknown_seen = ex.code() == vm1::VmTrapCode::unknown_opcode;
    }
    require(unknown_seen, "unknown opcode expected");

    std::cout << "vm1_exceptions OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vm1_exceptions failed: " << ex.what() << '\n';
    return 1;
  }
}
