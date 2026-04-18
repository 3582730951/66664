#include "test_common.h"

using namespace vmp::tests::runtime_vm2_jit;

int main() {
  auto c_module = assemble_text(fib20_program());
  auto c_result = run_module(c_module, "c", 40, {20});
  require(c_result.ret_int == 6765, "c backend mismatch");
  if (host_supports_x64_backend()) {
    auto x_module = assemble_text(fib20_program());
    auto x_result = run_module(x_module, "x64", 40, {20});
    require(x_result.ret_int == c_result.ret_int, "x64 backend parity mismatch");
  }
  std::cout << "vm2_jit_backend_parity OK\n";
}
