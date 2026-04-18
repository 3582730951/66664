#include "test_common.h"

using namespace vmp::tests::runtime_vm2_jit;

int main() {
  auto module = assemble_text(fib20_program());
  vmp::runtime::vm2::Vm2Context context(module);
  context.r[0] = 20;
  EnvGuard backend_guard("VMP_JIT_BACKEND", "c");
  vmp::runtime::jit::Vm2Jit::instance().reset_for_tests();
  vmp::runtime::vm2::Vm2Interpreter interpreter;
  for (int i = 0; i < 40; ++i) {
    context = vmp::runtime::vm2::Vm2Context(module);
    context.r[0] = 20;
    auto result = interpreter.execute(context);
    require(result.ret_int == 6765, "fib20 result mismatch");
  }
  require(vmp::runtime::jit::Vm2Jit::instance().module_entry_count(module.id()) >= 1, "expected compiled vm2 function");
  std::cout << "vm2_jit_correctness_fib20 OK\n";
}
