#include "test_common.h"

#include <iostream>

using namespace vmp::tests::runtime_vm1_jit;

int main() {
  auto pool = make_string_pool({{0, "secret-jit-string"}});
  auto module = vmp::runtime::vm1::assemble_module_text(R"(
entry:
  load_tstr vr1, &sid0
  mov vr2, vr1
  release_tstr vr1
  mov vr0, vr2
  ret
)");
  vmp::runtime::vm1::Vm1Context context(module);
  {
    EnvGuard backend_guard("VMP_JIT_BACKEND", "c");
    vmp::runtime::jit::Vm1Jit::instance().reset_for_tests();
    context.string_pool = pool;
    vmp::runtime::vm1::Vm1Interpreter interpreter;
    const auto result = interpreter.execute(context);
    require(result.ret_int != 0, "expected transient handle");
    require(context.debug_last_release_zeroed(result.ret_int), "released transient bytes were not zeroized");
    require(context.active_transient_strings() == 0, "transient handle leaked after release");
  }
  std::cout << "jit_string_barrier OK\n";
}
