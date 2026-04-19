#include "test_common.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <vmp/arch/arm/arm.h>
#include <vmp/arch/arm/ir.h>
#include <vmp/arch/common/pc_relative.h>
#include <vmp/runtime/vm1/vm1.h>

namespace arm = vmp::arch::arm;
namespace common = vmp::arch::common;
namespace vm1 = vmp::runtime::vm1;

namespace {
using vmp::tests::arch::require;

struct Case {
  std::string name;
  std::vector<std::uint8_t> bytes;
  arm::ExecutionMode mode = arm::ExecutionMode::arm;
  std::uint64_t address = 0;
  common::PcRelativeTarget::Kind kind = common::PcRelativeTarget::Kind::branch;
  std::uint64_t source_pc = 0;
  std::int64_t displacement = 0;
  std::uint64_t absolute = 0;
};

std::vector<std::uint8_t> le16(std::uint16_t value) {
  return {static_cast<std::uint8_t>(value & 0xFFu), static_cast<std::uint8_t>((value >> 8) & 0xFFu)};
}

std::vector<std::uint8_t> le32(std::uint32_t value) {
  return {static_cast<std::uint8_t>(value & 0xFFu), static_cast<std::uint8_t>((value >> 8) & 0xFFu),
          static_cast<std::uint8_t>((value >> 16) & 0xFFu), static_cast<std::uint8_t>((value >> 24) & 0xFFu)};
}

void verify_case(const Case& tc) {
  const auto ir = arm::decode_instruction(tc.bytes, tc.address, tc.mode);
  require(ir.pc_relative_target.has_value(), tc.name + ": missing pc_relative_target");
  const auto& target = *ir.pc_relative_target;
  require(target.kind == tc.kind, tc.name + ": wrong kind");
  require(target.source_pc == tc.source_pc, tc.name + ": wrong source_pc");
  require(target.displacement == tc.displacement, tc.name + ": wrong displacement");
  require(target.computed_absolute == tc.absolute, tc.name + ": wrong computed_absolute");
  require(arm::reencode_instruction(ir) == ir.encoding, tc.name + ": reencode mismatch");
}

void verify_integration() {
  common::FunctionView view;
  view.base_addr = 0x3000;
  view.cc = common::CallingConvention::aapcs32;
  view.endian = common::ArchEndianness::little;
  view.code = {
      0x00, 0x00, 0x00, 0xEA,  // b +0 -> skip next add, land on mov
      0x01, 0x00, 0x80, 0xE0,  // add r0, r0, r1 (skipped)
      0x01, 0x00, 0xA0, 0xE1,  // mov r0, r1
      0x1E, 0xFF, 0x2F, 0xE1,  // bx lr
  };

  arm::ArmLifter lifter;
  const auto lifted = lifter.lift(view);
  require(lifted.ok(), "arm pc-relative lift failed");
  require(vmp::tests::arch::run_vm1_abi(lifted.module, view.cc, {2, 7}) == 7,
          "arm branch integration returned unexpected value");
  const auto text = vm1::disassemble_module(lifted.module);
  require(text.find("jmp @") != std::string::npos, "arm disassembly missing jump label");
}

std::vector<Case> build_cases() {
  return {
      {"arm_b_zero", le32(0xEA000000u), arm::ExecutionMode::arm, 0x1000,
       common::PcRelativeTarget::Kind::branch, 0x1008, 0, 0x1008},
      {"arm_bl_zero", le32(0xEB000000u), arm::ExecutionMode::arm, 0x1100,
       common::PcRelativeTarget::Kind::call, 0x1108, 0, 0x1108},
      {"arm_b_back", le32(0xEAFFFFFEu), arm::ExecutionMode::arm, 0x1200,
       common::PcRelativeTarget::Kind::branch, 0x1208, -8, 0x1200},
      {"arm_blx_imm_zero", le32(0xFA000000u), arm::ExecutionMode::arm, 0x1300,
       common::PcRelativeTarget::Kind::call, 0x1308, 0, 0x1308},
      {"arm_ldr_literal_pos", le32(0xE59F0000u), arm::ExecutionMode::arm, 0x1400,
       common::PcRelativeTarget::Kind::load, 0x1408, 0, 0x1408},
      {"arm_ldr_literal_neg", le32(0xE51F0004u), arm::ExecutionMode::arm, 0x1500,
       common::PcRelativeTarget::Kind::load, 0x1508, -4, 0x1504},
      {"arm_bx_r3", le32(0xE12FFF13u), arm::ExecutionMode::arm, 0x1600,
       common::PcRelativeTarget::Kind::indirect_jump_via_table, 0x1608, 0, 0x1608},
      {"arm_mov_pc_r3", le32(0xE1A0F003u), arm::ExecutionMode::arm, 0x1700,
       common::PcRelativeTarget::Kind::indirect_jump_via_table, 0x1708, 0, 0x1708},
      {"thumb_cbz_zero", le16(0xB100u), arm::ExecutionMode::thumb, 0x1800,
       common::PcRelativeTarget::Kind::branch, 0x1804, 0, 0x1804},
      {"thumb_cbnz_zero", le16(0xB900u), arm::ExecutionMode::thumb, 0x1810,
       common::PcRelativeTarget::Kind::branch, 0x1814, 0, 0x1814},
      {"thumb_b_zero", le16(0xE000u), arm::ExecutionMode::thumb, 0x1820,
       common::PcRelativeTarget::Kind::branch, 0x1824, 0, 0x1824},
      {"thumb_b_back", le16(0xE7FEu), arm::ExecutionMode::thumb, 0x1830,
       common::PcRelativeTarget::Kind::branch, 0x1834, -4, 0x1830},
      {"thumb_ldr_literal_zero", le16(0x4800u), arm::ExecutionMode::thumb, 0x1842,
       common::PcRelativeTarget::Kind::load, 0x1844, 0, 0x1844},
      {"thumb_tbb_pc_r0", {0xDF, 0xE8, 0x00, 0xF0}, arm::ExecutionMode::thumb, 0x1850,
       common::PcRelativeTarget::Kind::indirect_jump_via_table, 0x1854, 0, 0x1854},
      {"thumb_tbh_pc_r0", {0xDF, 0xE8, 0x10, 0xF0}, arm::ExecutionMode::thumb, 0x1860,
       common::PcRelativeTarget::Kind::indirect_jump_via_table, 0x1864, 0, 0x1864},
  };
}
}  // namespace

int main() {
  try {
    for (const auto& tc : build_cases()) {
      verify_case(tc);
    }
    verify_integration();
    std::cout << "pc_relative_arm OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "pc_relative_arm failed: " << ex.what() << '\n';
    return 1;
  }
}
