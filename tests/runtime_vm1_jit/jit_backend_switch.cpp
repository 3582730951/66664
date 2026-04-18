#include "test_common.h"

#include <iostream>

using namespace vmp::tests::runtime_vm1_jit;

static std::uint64_t run_for_backend(vmp::runtime::vm1::Vm1Module& module, const std::string& backend) {
  return run_module_reuse(module, backend, 3);
}

int main() {
  auto module = vmp::runtime::vm1::assemble_module_text(R"(
entry:
  ldi_u64 vr0, 1
  ldi_u64 vr1, 2
  add vr0, vr0, vr1
  ret
)");
  const auto c_result = run_for_backend(module, "c");
  require(c_result == 3, "c backend wrong result");
  if (host_supports_x64_backend()) {
    const auto x64_result = run_for_backend(module, "x64");
    require(x64_result == c_result, "x64 backend result mismatch");
  }
  std::cout << "jit_backend_switch OK\n";
}
