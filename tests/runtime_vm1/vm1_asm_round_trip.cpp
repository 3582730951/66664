#include "test_common.h"

#include <iostream>

using namespace vmp::tests::runtime_vm1;
namespace vm1 = vmp::runtime::vm1;

int main() {
  try {
    const std::string program = R"(
start:
  ldi_u64 vr0, 42
  ldi64 vr1, -7
  add vr2, vr0, vr1
  jeq vr2, vr1, @skip
  load_transient_string vr3, 0
  breakpoint
skip:
  call @func, 2
  ret
func:
  mul vr0, vr0, vr2
  ret
.const string 0 "hello"
)";
    const auto module = vm1::assemble_module_text(program);
    const auto bytes = module.serialize();
    const auto parsed = vm1::Vm1Module::load_from_bytes(bytes);
    const auto text = vm1::disassemble_module(parsed);
    const auto reparsed = vm1::assemble_module_text(text);
    require(parsed.serialize() == reparsed.serialize(), "round-trip bytes mismatch");
    std::cout << "vm1_asm_round_trip OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vm1_asm_round_trip failed: " << ex.what() << '\n';
    return 1;
  }
}
