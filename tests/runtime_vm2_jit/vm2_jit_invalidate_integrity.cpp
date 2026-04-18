#include "test_common.h"

using namespace vmp::tests::runtime_vm2_jit;

int main() {
  auto module = assemble_text(fib20_program());
  auto result = run_module(module, "c", 40, {20});
  require(result.ret_int == 6765, "warmup mismatch");
  auto& jit = vmp::runtime::jit::Vm2Jit::instance();
  require(jit.module_entry_count(module.id()) > 0, "expected compiled entries before runtime event");
  vmp::runtime::state::RuntimeState::instance().observe(vmp::runtime::state::RuntimeEventKind::integrity_failed);
  require(jit.module_entry_count(module.id()) == 0, "integrity_failed should clear vm2 jit");
  std::cout << "vm2_jit_invalidate_integrity OK\n";
}
