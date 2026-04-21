#include "test_common.h"

#include <iostream>

#include <vmp/runtime/vm2/vm2.h>

using namespace vmp::tests::runtime_polymorphic_handlers;

int main() {
  try {
    const auto& layout = vmp::runtime::vm2::polymorphic_handler_layout();
    require(layout.build_seed != 0, "vm2 build seed must be non-zero");
    require(layout.layout_fingerprint != 0, "vm2 layout fingerprint must be non-zero");
    require_permutation(layout, vmp::runtime::vm2::canonical_opcode_sequence(), "vm2");
    require(vmp::runtime::vm2::polymorphic_handler_layout_fingerprint() == layout.layout_fingerprint,
            "vm2 fingerprint accessor mismatch");
    std::cout << "polymorphic_vm2_layout OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "polymorphic_vm2_layout failed: " << ex.what() << '\n';
    return 1;
  }
}
