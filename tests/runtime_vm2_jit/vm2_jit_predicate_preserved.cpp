#include "test_common.h"

using namespace vmp::tests::runtime_vm2_jit;

int main() {
  auto program = R"(
entry:
  ildimm r0, 9
  blnk @worker, 1
  xcall native, 1, 1, 0, 0
  bret
worker:
  ildimm r1, 9
  isub r0, r0, r1
  bret
)";

  auto native_check = [](const vmp::runtime::bridge::DomainCallArgs& args) {
    return vmp::runtime::bridge::DomainCallResult{args.ints.at(0), 0.0, 0};
  };

  vmp::runtime::bridge::BridgeRegistry bridge_interp;
  bridge_interp.register_native(1, native_check);
  auto interp_module = assemble_text(program);
  vmp::runtime::vm2::Vm2Context interp_ctx(interp_module);
  interp_ctx.bridge_registry = &bridge_interp;
  vmp::runtime::vm2::Vm2Interpreter interpreter;
  const auto interp_result = interpreter.execute(interp_ctx);

  vmp::runtime::bridge::BridgeRegistry bridge_jit;
  bridge_jit.register_native(1, native_check);
  auto jit_module = assemble_text(program);
  auto jit_result = run_module(jit_module, "c", 40, {}, &bridge_jit);
  require(interp_result.ret_int == jit_result.ret_int, "predicate-observed result mismatch");
  std::cout << "vm2_jit_predicate_preserved OK\n";
}
