#include "test_common.h"

using namespace vmp::tests::runtime_vm2_jit;

int main() {
  auto module = assemble_text(fib20_program());
  const auto fib_pc = single_non_entry_function(module);
  auto result = run_module(module, "c", 40, {20});
  require(result.ret_int == 6765, "warmup mismatch");
  const auto stats = vmp::runtime::jit::Vm2Jit::instance().entry_stats(module.id(), fib_pc);
  require(stats.entry_trampoline_hits > 0, "expected trampoline hit count");
  require(stats.native_degrade_count == 0, "vm2 jit must not degrade to native");
  std::cout << "vm2_jit_no_degrade_to_native OK\n";
}
