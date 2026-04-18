#include "test_common.h"

#include <iostream>

using namespace vmp::tests::runtime_vm2;
namespace vm2 = vmp::runtime::vm2;

int main() {
  try {
    bool div_zero = false;
    try {
      (void)run_text(R"(
        ildimm r0, 1
        ildimm r1, 0
        idiv r0, r0, r1
        bret
      )");
    } catch (const vm2::Vm2DivByZero&) {
      div_zero = true;
    }
    require(div_zero, "div zero should throw");

    bool bad_op = false;
    try {
      vm2::Vm2Module mod;
      mod.code = {0xff, 0xff};
      vm2::Vm2Context ctx(mod);
      vm2::Vm2Interpreter{}.execute(ctx);
    } catch (const vm2::Vm2UnknownOpcode&) {
      bad_op = true;
    }
    require(bad_op, "unknown opcode should throw");

    bool stack_overflow = false;
    try {
      auto module = vm2::assemble_module_text(R"(
        blnk @f, 9
        bret
f:
        blnk @f, 9
        bret
      )");
      vm2::Vm2Context ctx(module, 256);
      for (std::size_t i = 0; i < 9; ++i) ctx.r[i] = 1;
      vm2::Vm2Interpreter{}.execute(ctx);
    } catch (const vm2::Vm2StackOverflow&) {
      stack_overflow = true;
    }
    require(stack_overflow, "stack overflow should throw");

    std::cout << "vm2_exceptions OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vm2_exceptions failed: " << ex.what() << '\n';
    return 1;
  }
}
