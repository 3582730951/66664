#include "test_common.h"

#include <iostream>

using namespace vmp::tests::runtime_vm1;
namespace bridge = vmp::runtime::bridge;
namespace vm1 = vmp::runtime::vm1;

int main() {
  try {
    bridge::BridgeRegistry registry;
    registry.register_native(7, [](const bridge::DomainCallArgs& args) {
      return bridge::DomainCallResult{args.ints.at(0) + args.ints.at(1), 0.0, 0};
    });

    auto plus10_module = vm1::assemble_module_text(R"(
      ldi_u64 vr1, 10
      add vr0, vr0, vr1
      domain_ret
    )");
    registry.register_vm1(100, &plus10_module);

    auto vm_calls_native = vm1::assemble_module_text(R"(
      domain_call native, 7, 2
      ret
    )");
    auto vm_calls_vm = vm1::assemble_module_text(R"(
      domain_call vm1, 100, 1
      ret
    )");

    {
      vm1::Vm1Context ctx(vm_calls_native);
      ctx.vr[0] = 20;
      ctx.vr[1] = 22;
      ctx.bridge_registry = &registry;
      vm1::Vm1Interpreter interpreter;
      require_int(interpreter.execute(ctx).ret_int, 42, "vm1_to_native");
    }

    {
      auto result = registry.call(bridge::Domain::vm1, 100, bridge::DomainCallArgs{{32}, {}, {}}, 64);
      require_int(result.ret_int, 42, "native_to_vm1");
    }

    {
      vm1::Vm1Context ctx(vm_calls_vm);
      ctx.vr[0] = 32;
      ctx.bridge_registry = &registry;
      vm1::Vm1Interpreter interpreter;
      require_int(interpreter.execute(ctx).ret_int, 42, "vm1_to_vm1");
    }

    registry.register_native(99, [&](const bridge::DomainCallArgs& args) {
      return registry.call(bridge::Domain::vm1, 100, bridge::DomainCallArgs{{args.ints.at(0)}, {}, {}}, 64);
    });
    {
      auto nested = vm1::assemble_module_text(R"(
        domain_call native, 99, 1
        ret
      )");
      vm1::Vm1Context ctx(nested);
      ctx.vr[0] = 32;
      ctx.bridge_registry = &registry;
      vm1::Vm1Interpreter interpreter;
      require_int(interpreter.execute(ctx).ret_int, 42, "vm1_native_vm1_chain");
    }

    bool depth_failed = false;
    registry.register_native(123, [&](const bridge::DomainCallArgs& args) {
      return registry.call(bridge::Domain::native, 123, args, 2);
    });
    try {
      (void)registry.call(bridge::Domain::native, 123, bridge::DomainCallArgs{{1}, {}, {}}, 2);
    } catch (const bridge::BridgeException&) {
      depth_failed = true;
    }
    require(depth_failed, "max_depth should throw");

    std::cout << "vm1_cross_domain_call OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vm1_cross_domain_call failed: " << ex.what() << '\n';
    return 1;
  }
}
