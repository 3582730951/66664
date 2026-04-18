#include "test_common.h"

#include <iostream>

#include <vmp/runtime/vm1/vm1.h>

using namespace vmp::tests::runtime_vm2;
namespace vm1 = vmp::runtime::vm1;
namespace vm2 = vmp::runtime::vm2;

int main() {
  try {
    auto m1 = vm1::assemble_module_text("ldi_u64 vr0, 1\nret\n");
    auto m2 = vm2::assemble_module_text("ildimm r0, 1\nbret\n");
    require(vm1::kVm1Magic != vm2::kVm2Magic, "magic bytes must differ");

    bool vm2_reject_vm1 = false;
    try {
      (void)vm2::Vm2Module::load_from_bytes(m1.serialize());
    } catch (const std::exception&) {
      vm2_reject_vm1 = true;
    }
    require(vm2_reject_vm1, "Vm2 must reject vm1 image");

    bool vm1_reject_vm2 = false;
    try {
      (void)vm1::Vm1Module::load_from_bytes(m2.serialize());
    } catch (const std::exception&) {
      vm1_reject_vm2 = true;
    }
    require(vm1_reject_vm2, "Vm1 must reject vm2 image");

    std::cout << "isa_isolation OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "isa_isolation failed: " << ex.what() << '\n';
    return 1;
  }
}
