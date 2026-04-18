#include "test_common.h"

using namespace vmp::tests::runtime_vm2_jit;

int main() {
  auto audit_path = temp_path("vm2_jit_integrity", ".log");
  vmp::runtime::audit::AuditWriter writer(audit_path.string());
  vmp::runtime::audit::ReactionDispatcher dispatcher(writer, vmp::runtime::audit::ReactionPolicy::audit_only);
  auto module = assemble_text(fib20_program());
  const auto fib_pc = single_non_entry_function(module);
  vmp::runtime::jit::Vm2Jit::instance().set_audit_writer(&writer);
  auto result = run_module(module, "c", 40, {20}, nullptr, &dispatcher);
  require(result.ret_int == 6765, "initial jit result mismatch");
  auto& jit = vmp::runtime::jit::Vm2Jit::instance();
  require(jit.debug_patch_code_byte(module.id(), fib_pc, 0, 0xCC), "failed to patch compiled code");
  vmp::runtime::vm2::Vm2Context context(module);
  context.r[0] = 20;
  vmp::runtime::vm2::Vm2Interpreter interpreter;
  const auto fallback = interpreter.execute(context);
  writer.flush();
  require(fallback.ret_int == 6765, "fallback after integrity failure mismatch");
  require(!jit.has_entry(module.id(), fib_pc), "entry should be evicted after integrity failure");
  const auto log = read_all(audit_path);
  require(log.find("vm2_jit_integrity_failure") != std::string::npos, "missing integrity failure audit");
  std::cout << "vm2_jit_integrity_failure_evicts OK\n";
}
