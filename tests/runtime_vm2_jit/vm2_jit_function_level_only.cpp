#include "test_common.h"

using namespace vmp::tests::runtime_vm2_jit;

int main() {
  auto module = assemble_text(R"(
entry:
  ildimm r1, 1
  blnk @worker, 1
  bret
worker:
  ildimm r2, 10
loop:
  isub r2, r2, r1
  jnp p0, @loop
  bret
)");
  auto result = run_module(module, "c", 40);
  require(result.ret_int == 0, "unexpected result");
  auto& jit = vmp::runtime::jit::Vm2Jit::instance();
  const auto worker_pc = single_non_entry_function(module);
  require(jit.has_entry(module.id(), module.entry_pc), "entry pc missing from jit table");
  require(jit.has_entry(module.id(), worker_pc), "worker entry missing from jit table");
  require(jit.module_entry_count(module.id()) == module.function_entries.size(), "jit cache should contain only function entries");
  std::cout << "vm2_jit_function_level_only OK\n";
}
