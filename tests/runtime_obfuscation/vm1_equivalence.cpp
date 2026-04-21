#include "test_common.h"

#include <iostream>

int main() {
  try {
    const std::string program = R"(
entry:
  ldi_u64 vr0, 7
  ldi_u64 vr1, 5
  add vr0, vr0, vr1
  ldi_u64 vr2, 3
  sub vr0, vr0, vr2
  ret
)";

    const auto plain = vmp::runtime::vm1::assemble_module_text(program);

    vmp::runtime::vm1::AssembleOptions options;
    options.enable_mba_obfuscation = true;
    options.enable_opaque_predicates = true;
    options.obfuscation_depth = 3;
    const auto obfuscated = vmp::runtime::vm1::assemble_module_text(program, options);

    vmp::runtime::vm1::Vm1Context plain_ctx(plain);
    vmp::runtime::vm1::Vm1Context obf_ctx(obfuscated);
    const auto plain_result = vmp::runtime::vm1::Vm1Interpreter{}.execute(plain_ctx).ret_int;
    const auto obf_result = vmp::runtime::vm1::Vm1Interpreter{}.execute(obf_ctx).ret_int;

    if (plain_result != obf_result) {
      throw std::runtime_error("vm1 obfuscated result mismatch");
    }
    if (obfuscated.code.size() <= plain.code.size()) {
      throw std::runtime_error("vm1 obfuscated bytecode did not grow");
    }
    if (vmp::runtime::vm1::instruction_lengths(obfuscated).size() <=
        vmp::runtime::vm1::instruction_lengths(plain).size()) {
      throw std::runtime_error("vm1 obfuscated instruction stream did not grow");
    }

    std::cout << "runtime_obfuscation_vm1_equivalence OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "runtime_obfuscation_vm1_equivalence failed: " << ex.what() << '\n';
    return 1;
  }
}
