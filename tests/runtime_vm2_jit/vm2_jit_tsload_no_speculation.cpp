#include "test_common.h"

using namespace vmp::tests::runtime_vm2_jit;

int main() {
  auto key_context = fixed_key_context_ptr();
  std::vector<std::uint8_t> salt;
  auto pool = make_string_pool({{1, "secret-vm2"}}, &salt);

  auto module = assemble_text(R"(
entry:
  blnk @worker, 0
  bret
worker:
  tsload r0, 1
  tsrelease r0
  ildimm r0, 7
  bret
)");
  vmp::runtime::vm2::Vm2Context key_probe(module);
  key_probe.key_context = key_context;
  module.key_context_id = key_probe.current_key_context_id();

  vmp::runtime::vm2::Vm2Context context(module);
  context.string_pool = pool;
  context.key_context = key_context;
  EnvGuard backend_guard("VMP_JIT_BACKEND", "c");
  auto& jit = vmp::runtime::jit::Vm2Jit::instance();
  jit.reset_for_tests();
  vmp::runtime::vm2::Vm2Interpreter interpreter;
  for (int i = 0; i < 40; ++i) {
    context = vmp::runtime::vm2::Vm2Context(module);
    context.string_pool = pool;
    context.key_context = key_context;
    const auto result = interpreter.execute(context);
    require(result.ret_int == 7, "tsload program result mismatch");
    require(context.active_transient_strings() == 0, "transient string leaked across jit");
  }
  std::cout << "vm2_jit_tsload_no_speculation OK\n";
}
