#include "test_common.h"

#include <atomic>
#include <iostream>

using namespace vmp::tests::runtime_vm1_jit;

int main() {
  std::atomic<bool> native_seen{false};
  vmp::runtime::bridge::BridgeRegistry bridge;
  bridge.register_native(42, [&](const vmp::runtime::bridge::DomainCallArgs& args) {
    std::atomic_thread_fence(std::memory_order_seq_cst);
    native_seen.store(true, std::memory_order_release);
    return vmp::runtime::bridge::DomainCallResult{args.ints.at(0) + 11, 0.0, 0};
  });
  auto module = vmp::runtime::vm1::assemble_module_text(R"(
entry:
  ldi_u64 vr0, 31
  domain_call native, 42, 1
  ret
)");
  const auto result = run_module_reuse(module, "c", 3, &bridge, nullptr);
  require(native_seen.load(std::memory_order_acquire), "native bridge target was not called");
  require(result == 42, "unexpected domain call result");
  std::cout << "jit_domain_call_no_inlining OK\n";
}
