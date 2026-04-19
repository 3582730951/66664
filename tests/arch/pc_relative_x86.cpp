#include "test_common.h"

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <vmp/arch/common/pc_relative.h>
#include <vmp/arch/x86/ir.h>
#include <vmp/arch/x86/x86.h>
#include <vmp/runtime/vm1/vm1.h>

namespace common = vmp::arch::common;
namespace x86 = vmp::arch::x86;
namespace vm1 = vmp::runtime::vm1;

namespace {
using vmp::tests::arch::require;

struct Case {
  std::string name;
  std::vector<std::uint8_t> bytes;
  std::uint64_t address = 0;
  common::PcRelativeTarget::Kind kind = common::PcRelativeTarget::Kind::branch;
  std::uint64_t source_pc = 0;
  std::int64_t displacement = 0;
  std::uint64_t absolute = 0;
};

void verify_case(const Case& tc) {
  const auto ir = x86::decode_instruction(tc.bytes, tc.address);
  require(ir.pc_relative_target.has_value(), tc.name + ": missing pc_relative_target");
  const auto& target = *ir.pc_relative_target;
  require(target.kind == tc.kind, tc.name + ": wrong kind");
  require(target.source_pc == tc.source_pc, tc.name + ": wrong source_pc");
  require(target.displacement == tc.displacement, tc.name + ": wrong displacement");
  require(target.computed_absolute == tc.absolute, tc.name + ": wrong computed_absolute");
  require(x86::reencode_instruction(ir) == tc.bytes, tc.name + ": reencode mismatch");
  if (tc.kind == common::PcRelativeTarget::Kind::branch || tc.kind == common::PcRelativeTarget::Kind::call) {
    if (tc.bytes.size() == 2) {
      require(common::encode_rel8(target.source_pc, target.computed_absolute) == target.displacement,
              tc.name + ": rel8 helper mismatch");
    } else {
      require(common::encode_rel32(target.source_pc, target.computed_absolute) == target.displacement,
              tc.name + ": rel32 helper mismatch");
    }
  }
}

void verify_integration() {
  common::FunctionView view;
  view.base_addr = 0x2000;
  view.cc = common::CallingConvention::cdecl_x86;
  view.endian = common::ArchEndianness::little;
  view.code = {
      0x55,             // push ebp
      0x8B, 0xEC,       // mov ebp, esp
      0xEB, 0x03,       // jmp skip
      0x03, 0x45, 0x08, // add eax, [ebp+8] (skipped)
      0x8B, 0x45, 0x08, // skip: mov eax, [ebp+8]
      0x5D,             // pop ebp
      0xC3,
  };

  x86::X86Lifter lifter;
  const auto lifted = lifter.lift(view);
  require(lifted.ok(), "x86 pc-relative lift failed");
  require(vmp::tests::arch::run_vm1_abi(lifted.module, view.cc, {7}) == 7,
          "x86 pc-relative jump integration returned unexpected value");
  const auto text = vm1::disassemble_module(lifted.module);
  require(text.find("jmp @") != std::string::npos, "x86 disassembly missing jump label");
}

std::vector<Case> build_cases() {
  return {
      {"jmp_rel8_fwd", {0xEB, 0x05}, 0x1000,
       common::PcRelativeTarget::Kind::branch, 0x1002, 5, 0x1007},
      {"jmp_rel32_fwd", {0xE9, 0x34, 0x12, 0x00, 0x00}, 0x1100,
       common::PcRelativeTarget::Kind::branch, 0x1105, 0x1234, 0x2339},
      {"je_rel8_back", {0x74, 0xF8}, 0x1200,
       common::PcRelativeTarget::Kind::branch, 0x1202, -8, 0x11FA},
      {"jne_rel32_back", {0x0F, 0x85, 0xF0, 0xFF, 0xFF, 0xFF}, 0x1300,
       common::PcRelativeTarget::Kind::branch, 0x1306, -16, 0x12F6},
      {"call_rel32_fwd", {0xE8, 0x08, 0x00, 0x00, 0x00}, 0x1400,
       common::PcRelativeTarget::Kind::call, 0x1405, 8, 0x140D},
      {"loopne_rel8", {0xE0, 0x7E}, 0x1500,
       common::PcRelativeTarget::Kind::branch, 0x1502, 126, 0x1580},
      {"loope_rel8", {0xE1, 0xFE}, 0x1600,
       common::PcRelativeTarget::Kind::branch, 0x1602, -2, 0x1600},
      {"loop_rel8", {0xE2, 0x10}, 0x1700,
       common::PcRelativeTarget::Kind::branch, 0x1702, 16, 0x1712},
      {"jecxz_rel8", {0xE3, 0x20}, 0x1800,
       common::PcRelativeTarget::Kind::branch, 0x1802, 32, 0x1822},
      {"jmp_table_abs", {0xFF, 0x25, 0x78, 0x56, 0x34, 0x12}, 0x1900,
       common::PcRelativeTarget::Kind::indirect_jump_via_table, 0, 0x12345678ll, 0x12345678},
      {"call_table_abs", {0xFF, 0x15, 0x34, 0x12, 0x00, 0x00}, 0x1A00,
       common::PcRelativeTarget::Kind::indirect_jump_via_table, 0, 0x1234, 0x1234},
      {"jmp_rel8_back_2", {0xEB, 0xF0}, 0x1B00,
       common::PcRelativeTarget::Kind::branch, 0x1B02, -16, 0x1AF2},
      {"call_rel32_back", {0xE8, 0xF8, 0xFF, 0xFF, 0xFF}, 0x1C00,
       common::PcRelativeTarget::Kind::call, 0x1C05, -8, 0x1BFD},
      {"jle_rel8", {0x7E, 0x7F}, 0x1D00,
       common::PcRelativeTarget::Kind::branch, 0x1D02, 127, 0x1D81},
      {"jg_rel32", {0x0F, 0x8F, 0x04, 0x00, 0x00, 0x00}, 0x1E00,
       common::PcRelativeTarget::Kind::branch, 0x1E06, 4, 0x1E0A},
  };
}
}  // namespace

int main() {
  try {
    for (const auto& tc : build_cases()) {
      verify_case(tc);
    }
    verify_integration();
    std::cout << "pc_relative_x86 OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "pc_relative_x86 failed: " << ex.what() << '\n';
    return 1;
  }
}
