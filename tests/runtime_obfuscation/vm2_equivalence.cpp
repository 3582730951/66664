#include "test_common.h"

#include <iostream>

int main() {
  try {
    const std::string program = R"(
entry:
  ildimm r0, 7
  ildimm r1, 5
  iadd r0, r0, r1
  ildimm r2, 3
  isub r0, r0, r2
  bret
)";

    const auto plain = vmp::runtime::vm2::assemble_module_text(program);

    vmp::runtime::vm2::AssembleOptions options;
    options.enable_mba_obfuscation = true;
    options.enable_opaque_predicates = true;
    options.obfuscation_depth = 3;
    const auto obfuscated = vmp::runtime::vm2::assemble_module_text(program, options);

    vmp::runtime::vm2::Vm2Context plain_ctx(plain);
    vmp::runtime::vm2::Vm2Context obf_ctx(obfuscated);
    const auto plain_result = vmp::runtime::vm2::Vm2Interpreter{}.execute(plain_ctx).ret_int;
    const auto obf_result = vmp::runtime::vm2::Vm2Interpreter{}.execute(obf_ctx).ret_int;

    if (plain_result != obf_result) {
      throw std::runtime_error("vm2 obfuscated result mismatch");
    }
    if (obfuscated.code.size() <= plain.code.size()) {
      throw std::runtime_error("vm2 obfuscated bytecode did not grow");
    }
    if (vmp::runtime::vm2::instruction_lengths(obfuscated).size() <=
        vmp::runtime::vm2::instruction_lengths(plain).size()) {
      throw std::runtime_error("vm2 obfuscated instruction stream did not grow");
    }

    std::cout << "runtime_obfuscation_vm2_equivalence OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "runtime_obfuscation_vm2_equivalence failed: " << ex.what() << '\n';
    return 1;
  }
}
