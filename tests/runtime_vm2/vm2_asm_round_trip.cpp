#include "test_common.h"

#include <iostream>

using namespace vmp::tests::runtime_vm2;
namespace vm2 = vmp::runtime::vm2;

int main() {
  try {
    const std::string program = R"(
.keyctx 0x00112233445566778899aabbccddeeff
.vconst c0, 0x10, 0x20
start:
  ildimm r0, 42
  ildimm r1, -7
  iadd r2, r0, r1
  jp p1, @skip
  vldimm q0, c0
  brk
skip:
  blnk @func, 2
  bret
func:
  imul r0, r0, r2
  bret
)";
    const auto module = vm2::assemble_module_text(program);
    const auto bytes = module.serialize();
    const auto parsed = vm2::Vm2Module::load_from_bytes(bytes);
    const auto text = vm2::disassemble_module(parsed);
    const auto reparsed = vm2::assemble_module_text(text);
    require(parsed.serialize() == reparsed.serialize(), "round-trip bytes mismatch");
    std::cout << "vm2_asm_round_trip OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vm2_asm_round_trip failed: " << ex.what() << '\n';
    return 1;
  }
}
