#include "test_common.h"

#include <iostream>

#include <vmp/runtime/vm1/vm1.h>

using namespace vmp::tests::runtime_vm2;
namespace bridge = vmp::runtime::bridge;
namespace vm1 = vmp::runtime::vm1;
namespace vm2 = vmp::runtime::vm2;

int main() {
  try {
    bridge::BridgeRegistry registry;
    registry.register_native(7, [](const bridge::DomainCallArgs& args) {
      return bridge::DomainCallResult{args.ints.at(0) + args.ints.at(1), 0.0, 0};
    });

    auto vm1_plus = vm1::assemble_module_text(R"(
      ldi_u64 vr1, 10
      add vr0, vr0, vr1
      domain_ret
    )");
    registry.register_vm1(100, &vm1_plus);

    auto vm2_plus = vm2::assemble_module_text(R"(
      ildimm r1, 10
      iadd r0, r0, r1
      xret
    )");
    registry.register_vm2(200, &vm2_plus);

    require_int(run_text(R"(
      xcall native, 7, 2
      bret
    )", {20, 22}, &registry).ret_int, 42, "vm2_to_native");

    require_int(registry.call(bridge::Domain::vm2, 200, bridge::DomainCallArgs{{32}, {}, {}}, 64).ret_int, 42,
                "native_to_vm2");

    require_int(run_text(R"(
      xcall vm1, 100, 1
      bret
    )", {32}, &registry).ret_int, 42, "vm2_to_vm1");

    auto vm1_to_vm2 = vm1::assemble_module_text(R"(
      domain_call vm2, 200, 1
      ret
    )");
    vm1::Vm1Context vm1_ctx(vm1_to_vm2);
    vm1_ctx.vr[0] = 32;
    vm1_ctx.bridge_registry = &registry;
    vm1::Vm1Interpreter vm1_interp;
    require_int(vm1_interp.execute(vm1_ctx).ret_int, 42, "vm1_to_vm2");

    require_int(run_text(R"(
      xcall vm2, 200, 1
      bret
    )", {32}, &registry).ret_int, 42, "vm2_to_vm2");

    bool depth_failed = false;
    auto vm2_loop = vm2::assemble_module_text(R"(
      xcall native, 124, 1
      bret
    )");
    registry.register_vm2(201, &vm2_loop);
    registry.register_native(124, [&](const bridge::DomainCallArgs& args) {
      return registry.call(bridge::Domain::vm2, 201, args, 2);
    });
    try {
      (void)registry.call(bridge::Domain::native, 124, bridge::DomainCallArgs{{1}, {}, {}}, 2);
    } catch (const bridge::BridgeException&) {
      depth_failed = true;
    }
    require(depth_failed, "max_depth should throw");

    std::cout << "vm2_cross_domain OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vm2_cross_domain failed: " << ex.what() << '\n';
    return 1;
  }
}
