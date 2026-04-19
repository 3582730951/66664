#include "test_common.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <vmp/arch/arm64/arm64.h>
#include <vmp/arch/arm64/ir.h>
#include <vmp/arch/common/pc_relative.h>
#include <vmp/runtime/vm1/vm1.h>

namespace arm64 = vmp::arch::arm64;
namespace common = vmp::arch::common;
namespace vm1 = vmp::runtime::vm1;

namespace {
using vmp::tests::arch::require;

struct Case {
  std::string name;
  std::uint32_t word = 0;
  std::uint64_t address = 0;
  common::PcRelativeTarget::Kind kind = common::PcRelativeTarget::Kind::branch;
  std::uint64_t source_pc = 0;
  std::int64_t displacement = 0;
  std::uint64_t absolute = 0;
};

std::vector<std::uint8_t> le32(std::uint32_t value) {
  return {static_cast<std::uint8_t>(value & 0xFFu), static_cast<std::uint8_t>((value >> 8) & 0xFFu),
          static_cast<std::uint8_t>((value >> 16) & 0xFFu), static_cast<std::uint8_t>((value >> 24) & 0xFFu)};
}

std::uint32_t enc_adr(bool page, std::uint8_t rd, std::int32_t imm21) {
  return (page ? 0x90000000u : 0x10000000u) | ((static_cast<std::uint32_t>(imm21) & 0x3u) << 29) |
         (((static_cast<std::uint32_t>(imm21) >> 2) & 0x7FFFFu) << 5) | rd;
}

std::uint32_t enc_b(bool link, std::int32_t imm26_words) {
  return (link ? 0x94000000u : 0x14000000u) | (static_cast<std::uint32_t>(imm26_words) & 0x03FFFFFFu);
}

std::uint32_t enc_bcond(arm64::ConditionCode cc, std::int32_t imm19_words) {
  return 0x54000000u | ((static_cast<std::uint32_t>(imm19_words) & 0x7FFFFu) << 5) | static_cast<std::uint32_t>(cc);
}

std::uint32_t enc_cbz(bool nz, std::uint8_t rt, std::int32_t imm19_words) {
  return (nz ? 0x35000000u : 0x34000000u) | ((static_cast<std::uint32_t>(imm19_words) & 0x7FFFFu) << 5) | rt;
}

std::uint32_t enc_tbz(bool nz, std::uint8_t rt, std::uint8_t bit, std::int32_t imm14_words) {
  return (nz ? 0x37000000u : 0x36000000u) | ((static_cast<std::uint32_t>(bit) & 0x3Fu) << 19) |
         ((static_cast<std::uint32_t>(imm14_words) & 0x3FFFu) << 5) | rt;
}

std::uint32_t enc_ldr_lit(bool is64, std::uint8_t rt, std::int32_t imm19_words) {
  return (is64 ? 0x58000000u : 0x18000000u) | ((static_cast<std::uint32_t>(imm19_words) & 0x7FFFFu) << 5) | rt;
}

void verify_case(const Case& tc) {
  const auto ir = arm64::decode_instruction(le32(tc.word), tc.address);
  require(ir.pc_relative_target.has_value(), tc.name + ": missing pc_relative_target");
  const auto& target = *ir.pc_relative_target;
  require(target.kind == tc.kind, tc.name + ": wrong kind");
  require(target.source_pc == tc.source_pc, tc.name + ": wrong source_pc");
  require(target.displacement == tc.displacement, tc.name + ": wrong displacement");
  require(target.computed_absolute == tc.absolute, tc.name + ": wrong computed_absolute");
  require(arm64::reencode_instruction(ir) == le32(tc.word), tc.name + ": reencode mismatch");
}

void verify_integration() {
  common::FunctionView view;
  view.base_addr = 0x4000;
  view.cc = common::CallingConvention::aapcs64;
  view.endian = common::ArchEndianness::little;
  view.code = {0x00, 0x00, 0x00, 0x10, 0xC0, 0x03, 0x5F, 0xD6};  // adr x0, #0 ; ret

  arm64::Arm64Lifter lifter;
  const auto lifted = lifter.lift(view);
  require(lifted.ok(), "arm64 pc-relative lift failed");
  require(vmp::tests::arch::run_vm1_abi(lifted.module, view.cc, {}) == 0,
          "arm64 adr integration returned unexpected value");
  const auto text = vm1::disassemble_module(lifted.module);
  require(text.find("ldi_u64 vr0, 0") != std::string::npos || text.find("ldi_u64 vr0, @") != std::string::npos,
          "arm64 disassembly missing label/immediate materialization");
}

std::vector<Case> build_cases() {
  return {
      {"b_zero", 0x14000000u, 0x1000, common::PcRelativeTarget::Kind::branch, 0x1000, 0, 0x1000},
      {"bl_zero", 0x94000000u, 0x1100, common::PcRelativeTarget::Kind::call, 0x1100, 0, 0x1100},
      {"b_back", enc_b(false, -1), 0x1200, common::PcRelativeTarget::Kind::branch, 0x1200, -4, 0x11FC},
      {"b_eq_zero", enc_bcond(arm64::ConditionCode::eq, 0), 0x1300, common::PcRelativeTarget::Kind::branch, 0x1300, 0, 0x1300},
      {"b_ne_fwd", enc_bcond(arm64::ConditionCode::ne, 2), 0x1310, common::PcRelativeTarget::Kind::branch, 0x1310, 8, 0x1318},
      {"cbz_zero", enc_cbz(false, 0, 0), 0x1400, common::PcRelativeTarget::Kind::branch, 0x1400, 0, 0x1400},
      {"cbnz_fwd", enc_cbz(true, 0, 2), 0x1410, common::PcRelativeTarget::Kind::branch, 0x1410, 8, 0x1418},
      {"tbz_zero", enc_tbz(false, 0, 0, 0), 0x1500, common::PcRelativeTarget::Kind::branch, 0x1500, 0, 0x1500},
      {"tbnz_fwd", enc_tbz(true, 0, 1, 2), 0x1510, common::PcRelativeTarget::Kind::branch, 0x1510, 8, 0x1518},
      {"adr_zero", 0x10000000u, 0x1600, common::PcRelativeTarget::Kind::address_materialize, 0x1600, 0, 0x1600},
      {"adr_back", enc_adr(false, 0, -4), 0x1610, common::PcRelativeTarget::Kind::address_materialize, 0x1610, -4, 0x160C},
      {"adrp_zero", 0x90000000u, 0x1704, common::PcRelativeTarget::Kind::address_materialize, 0x1704, 0, 0x1000},
      {"adrp_page_plus1", enc_adr(true, 0, 1), 0x1F00, common::PcRelativeTarget::Kind::address_materialize, 0x1F00, 4096, 0x2000},
      {"ldr_x_literal_zero", enc_ldr_lit(true, 0, 0), 0x1800, common::PcRelativeTarget::Kind::load, 0x1800, 0, 0x1800},
      {"ldr_w_literal_fwd", enc_ldr_lit(false, 0, 1), 0x1810, common::PcRelativeTarget::Kind::load, 0x1810, 4, 0x1814},
  };
}
}  // namespace

int main() {
  try {
    for (const auto& tc : build_cases()) {
      verify_case(tc);
    }
    verify_integration();
    std::cout << "pc_relative_arm64 OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "pc_relative_arm64 failed: " << ex.what() << '\n';
    return 1;
  }
}
