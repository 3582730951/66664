#include "test_common.h"

using namespace vmp::tests::runtime_vm2_jit;

int main() {
  auto audit_path = temp_path("vm2_jit_key_rotation", ".log");
  vmp::runtime::audit::AuditWriter writer(audit_path.string());
  auto module = assemble_text(fib20_program());
  auto result = run_module(module, "c", 40, {20});
  require(result.ret_int == 6765, "warmup mismatch");
  auto& jit = vmp::runtime::jit::Vm2Jit::instance();
  const auto before = jit.module_entry_count(module.id());
  require(before > 0, "expected compiled entries before key rotation");
  jit.set_audit_writer(&writer);
  jit.invalidate_module_for_key_context_change(module);
  require(jit.module_entry_count(module.id()) == 0, "module jit cache not cleared on key rotation");
  vmp::runtime::vm2::Vm2Context context(module);
  context.r[0] = 20;
  vmp::runtime::vm2::Vm2Interpreter interpreter;
  const auto rerun = interpreter.execute(context);
  writer.flush();
  require(rerun.ret_int == 6765, "rerun after key rotation mismatch");
  require(jit.module_entry_count(module.id()) > 0, "module should recompile after key rotation");
  const auto log = read_all(audit_path);
  require(log.find("vm2_jit_invalidate") != std::string::npos, "missing invalidate audit");
  std::cout << "vm2_jit_invalidate_key_rotation OK\n";
}
