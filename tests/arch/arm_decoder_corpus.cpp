#include "decoder_test_common.h"

#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <vector>

#include <vmp/arch/arm/arm.h>
#include <vmp/arch/arm/ir.h>

namespace dec = vmp::tests::arch::decoder;
namespace arm = vmp::arch::arm;
namespace common = vmp::arch::common;

namespace {
struct Case {
  std::vector<std::uint8_t> bytes;
  arm::ExecutionMode mode = arm::ExecutionMode::arm;
  std::string mnemonic;
  std::optional<arm::ConditionCode> cond;
  bool expect_mem = false;
  bool expect_rel = false;
};

std::vector<std::uint8_t> le32(std::uint32_t word) {
  return {static_cast<std::uint8_t>(word & 0xFFu), static_cast<std::uint8_t>((word >> 8) & 0xFFu),
          static_cast<std::uint8_t>((word >> 16) & 0xFFu), static_cast<std::uint8_t>((word >> 24) & 0xFFu)};
}
std::vector<std::uint8_t> le16(std::uint16_t word) {
  return {static_cast<std::uint8_t>(word & 0xFFu), static_cast<std::uint8_t>((word >> 8) & 0xFFu)};
}

std::uint32_t enc_dp(arm::ConditionCode cc, std::uint8_t opcode, bool imm, std::uint8_t rd, std::uint8_t rn, std::uint32_t op2) {
  return (static_cast<std::uint32_t>(cc) << 28) | (imm ? 1u << 25 : 0u) | (static_cast<std::uint32_t>(opcode) << 21) |
         (static_cast<std::uint32_t>(rn) << 16) | (static_cast<std::uint32_t>(rd) << 12) | (op2 & 0xFFFu);
}

std::uint32_t enc_b(arm::ConditionCode cc, bool link, std::int32_t imm24) {
  return (static_cast<std::uint32_t>(cc) << 28) | 0x0A000000u | (link ? 1u << 24 : 0u) | (static_cast<std::uint32_t>(imm24) & 0xFFFFFFu);
}

std::uint32_t enc_bx(bool link, std::uint8_t rm) {
  return 0xE12FFF10u | (link ? 0x20u : 0u) | rm;
}

std::uint32_t enc_mul(bool mla, std::uint8_t rd, std::uint8_t rs, std::uint8_t rm) {
  return 0xE0000090u | (mla ? (1u << 21) : 0u) | (static_cast<std::uint32_t>(rd) << 12) |
         (static_cast<std::uint32_t>(rs) << 8) | rm;
}

std::uint32_t enc_div(bool signed_div, std::uint8_t rd, std::uint8_t rn, std::uint8_t rm) {
  return (signed_div ? 0xE710F010u : 0xE730F010u) | (static_cast<std::uint32_t>(rd) << 16) |
         (static_cast<std::uint32_t>(rm) << 8) | rn;
}

std::uint32_t enc_ldrstr(bool load, bool reg_offset, bool pre, std::uint8_t rd, std::uint8_t rn, std::uint16_t imm12_or_rm) {
  return 0xE4000000u | (reg_offset ? (1u << 25) : 0u) | (pre ? (1u << 24) : 0u) | (load ? (1u << 20) : 0u) |
         (static_cast<std::uint32_t>(rn) << 16) | (static_cast<std::uint32_t>(rd) << 12) | imm12_or_rm;
}

std::uint32_t enc_ldmstm(bool load, std::uint8_t rn, std::uint16_t reglist) {
  return 0xE8000000u | (load ? (1u << 20) : 0u) | (static_cast<std::uint32_t>(rn) << 16) | reglist;
}

std::uint32_t enc_swp(std::uint8_t rd, std::uint8_t rn, std::uint8_t rm) {
  return 0xE1000090u | (static_cast<std::uint32_t>(rn) << 16) | (static_cast<std::uint32_t>(rd) << 12) | rm;
}

std::uint32_t enc_vmov(std::uint8_t sd, std::uint8_t rm) {
  return 0xEE000A10u | (static_cast<std::uint32_t>(sd) << 12) | rm;
}

std::uint32_t enc_vadd(std::uint8_t sd, std::uint8_t sn, std::uint8_t sm) {
  return 0xEE300A00u | (static_cast<std::uint32_t>(sn) << 16) | (static_cast<std::uint32_t>(sd) << 12) | sm;
}

void verify_case(const Case& tc, std::uint64_t address, std::size_t index) {
  auto ir = arm::decode_instruction(tc.bytes, address, tc.mode);
  if (ir.mnemonic != tc.mnemonic) {
    std::ostringstream detail;
    detail << "arm mnemonic mismatch case=" << index << " expected=" << tc.mnemonic << " actual=" << ir.mnemonic;
    throw std::runtime_error(detail.str());
  }
  dec::require(ir.mnemonic == tc.mnemonic, "arm mnemonic mismatch");
  dec::require(ir.mode == tc.mode, "arm mode mismatch");
  dec::require(ir.size == tc.bytes.size() || (tc.mode == arm::ExecutionMode::thumb && ir.size >= 2), "arm size mismatch");
  if (tc.cond.has_value()) dec::require(ir.condition == *tc.cond, "arm condition mismatch");
  if (tc.expect_mem && !ir.memory.valid) { std::ostringstream detail; detail << "arm expected memory operand case=" << index << " mnemonic=" << ir.mnemonic; throw std::runtime_error(detail.str()); }
  if (tc.expect_rel && !ir.has_relative_target) { std::ostringstream detail; detail << "arm expected relative target case=" << index << " mnemonic=" << ir.mnemonic; throw std::runtime_error(detail.str()); }
  dec::require(arm::reencode_instruction(ir) == ir.encoding, "arm reencode mismatch");
}

std::vector<Case> build_corpus() {
  std::vector<Case> cases;
  auto add_arm = [&](std::uint32_t word, std::string mnemonic, std::optional<arm::ConditionCode> cond = std::nullopt,
                     bool expect_mem = false, bool expect_rel = false) {
    cases.push_back(Case{le32(word), arm::ExecutionMode::arm, std::move(mnemonic), cond, expect_mem, expect_rel});
  };
  auto add_thumb16 = [&](std::uint16_t word, std::string mnemonic, bool expect_rel = false) {
    cases.push_back(Case{le16(word), arm::ExecutionMode::thumb, std::move(mnemonic), std::nullopt, false, expect_rel});
  };

  for (std::uint8_t rd = 0; rd < 8; ++rd) {
    add_arm(enc_dp(arm::ConditionCode::al, 0xD, false, rd, 0, static_cast<std::uint8_t>((rd + 1) & 0xFu)), "mov", arm::ConditionCode::al);
    add_arm(enc_dp(arm::ConditionCode::al, 0x4, false, rd, static_cast<std::uint8_t>((rd + 1) & 0xFu), static_cast<std::uint8_t>((rd + 2) & 0xFu)), "add", arm::ConditionCode::al);
    add_arm(enc_dp(arm::ConditionCode::al, 0x2, false, rd, static_cast<std::uint8_t>((rd + 3) & 0xFu), static_cast<std::uint8_t>((rd + 4) & 0xFu)), "sub", arm::ConditionCode::al);
    add_arm(enc_dp(arm::ConditionCode::al, 0x0, false, rd, static_cast<std::uint8_t>((rd + 5) & 0xFu), static_cast<std::uint8_t>((rd + 6) & 0xFu)), "and", arm::ConditionCode::al);
    add_arm(enc_dp(arm::ConditionCode::al, 0x1, false, rd, static_cast<std::uint8_t>((rd + 7) & 0xFu), static_cast<std::uint8_t>((rd + 1) & 0xFu)), "eor", arm::ConditionCode::al);
    add_arm(enc_dp(arm::ConditionCode::al, 0xC, false, rd, static_cast<std::uint8_t>((rd + 2) & 0xFu), static_cast<std::uint8_t>((rd + 3) & 0xFu)), "orr", arm::ConditionCode::al);
    add_arm(enc_dp(arm::ConditionCode::al, 0xA, false, rd, static_cast<std::uint8_t>((rd + 4) & 0xFu), static_cast<std::uint8_t>((rd + 5) & 0xFu)), "cmp", arm::ConditionCode::al);
    add_arm(enc_dp(arm::ConditionCode::al, 0x4, true, rd, static_cast<std::uint8_t>((rd + 1) & 0xFu), 0x12u + rd), "add", arm::ConditionCode::al);
  }
  for (std::uint8_t cond = 0; cond < 14; ++cond) {
    add_arm(enc_b(static_cast<arm::ConditionCode>(cond), false, 2), "b", static_cast<arm::ConditionCode>(cond), false, true);
  }
  add_arm(enc_b(arm::ConditionCode::al, true, 2), "bl", arm::ConditionCode::al, false, true);
  for (std::uint8_t rm = 0; rm < 8; ++rm) {
    add_arm(enc_bx(false, rm), "bx", arm::ConditionCode::al);
    add_arm(enc_bx(true, rm), "blx", arm::ConditionCode::al);
  }
  for (std::uint8_t rd = 0; rd < 8; ++rd) {
    add_arm(enc_mul(false, rd, static_cast<std::uint8_t>((rd + 1) & 0xFu), static_cast<std::uint8_t>((rd + 2) & 0xFu)), "mul", arm::ConditionCode::al);
    add_arm(enc_mul(true, rd, static_cast<std::uint8_t>((rd + 2) & 0xFu), static_cast<std::uint8_t>((rd + 3) & 0xFu)), "mla", arm::ConditionCode::al);
    add_arm(enc_div(true, rd, static_cast<std::uint8_t>((rd + 1) & 0xFu), static_cast<std::uint8_t>((rd + 2) & 0xFu)), "sdiv", arm::ConditionCode::al);
    add_arm(enc_div(false, rd, static_cast<std::uint8_t>((rd + 2) & 0xFu), static_cast<std::uint8_t>((rd + 3) & 0xFu)), "udiv", arm::ConditionCode::al);
  }
  for (std::uint8_t rd = 0; rd < 8; ++rd) {
    add_arm(enc_ldrstr(true, false, true, rd, static_cast<std::uint8_t>((rd + 1) & 0xFu), 0x20u + rd), "ldr", arm::ConditionCode::al, true);
    add_arm(enc_ldrstr(false, false, true, rd, static_cast<std::uint8_t>((rd + 2) & 0xFu), 0x30u + rd), "str", arm::ConditionCode::al, true);
    add_arm(enc_ldrstr(true, true, false, rd, static_cast<std::uint8_t>((rd + 3) & 0xFu), static_cast<std::uint8_t>((rd + 4) & 0xFu)), "ldr", arm::ConditionCode::al, true);
    add_arm(enc_ldrstr(false, true, false, rd, static_cast<std::uint8_t>((rd + 5) & 0xFu), static_cast<std::uint8_t>((rd + 6) & 0xFu)), "str", arm::ConditionCode::al, true);
    add_arm(enc_ldmstm(true, static_cast<std::uint8_t>((rd + 1) & 0xFu), static_cast<std::uint16_t>(1u << rd)), "ldm", arm::ConditionCode::al, true);
    add_arm(enc_ldmstm(false, static_cast<std::uint8_t>((rd + 2) & 0xFu), static_cast<std::uint16_t>(1u << rd)), "stm", arm::ConditionCode::al, true);
    add_arm(enc_swp(rd, static_cast<std::uint8_t>((rd + 1) & 0xFu), static_cast<std::uint8_t>((rd + 2) & 0xFu)), "swp", arm::ConditionCode::al, true);
  }
  for (std::uint8_t sd = 0; sd < 8; ++sd) {
    add_arm(enc_vmov(sd, static_cast<std::uint8_t>(sd & 0xFu)), "vmov", arm::ConditionCode::al);
    add_arm(enc_vadd(sd, static_cast<std::uint8_t>((sd + 1) & 0xFu), static_cast<std::uint8_t>((sd + 2) & 0xFu)), "vadd.f32", arm::ConditionCode::al);
  }
  add_arm(0xF3BF8F40u, "dsb", arm::ConditionCode::none);
  add_arm(0xF3BF8F50u, "dmb", arm::ConditionCode::none);
  add_arm(0xF3BF8F60u, "isb", arm::ConditionCode::none);
  add_arm(0x016F0F10u, "clrex", arm::ConditionCode::none);
  add_arm(0x0320F000u, "nop", arm::ConditionCode::none);

  for (std::uint16_t rdn = 0; rdn < 8; ++rdn) {
    const auto add = static_cast<std::uint16_t>(0x1800u | (rdn & 7u) | ((rdn & 7u) << 3) | ((rdn & 7u) << 6));
    add_thumb16(add, "add");
    add_thumb16(static_cast<std::uint16_t>(0xB100u | (rdn & 7u) | (((rdn + 1) & 0x1Fu) << 3)), "cbz", true);
    add_thumb16(static_cast<std::uint16_t>(0xB900u | (rdn & 7u) | (((rdn + 2) & 0x1Fu) << 3)), "cbnz", true);
    add_thumb16(static_cast<std::uint16_t>(0xE000u | ((rdn + 1) & 0x7FFu)), "b", true);
    add_thumb16(static_cast<std::uint16_t>(0x4800u | ((rdn & 7u) << 8) | ((rdn + 1) & 0xFFu)), "ldr");
  }

  dec::require(cases.size() >= 200, "arm corpus too small");
  return cases;
}

void run_real_binary_probe() {
  const auto dir = std::filesystem::temp_directory_path() / "vmp_arm_decoder_probe";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const auto source = dec::write_c_source(dir, "hello_arm.c", dec::hello_source());
  const auto binary = dir / "hello_arm.o";
  std::ostringstream cmd;
  cmd << "arm-linux-gnueabihf-gcc -O0 -marm -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard -c "
      << dec::shell_quote(source) << " -o " << dec::shell_quote(binary);
  dec::run_command_checked(cmd.str());
  const auto text = dec::load_text_section(binary, 1024);
  std::vector<common::Diagnostic> diagnostics;
  const auto decoded = arm::decode_stream(text.bytes, text.address, arm::ExecutionMode::arm, &diagnostics);
  std::size_t unsupported = 0;
  for (const auto& d : diagnostics) if (d.kind == common::DiagnosticKind::unsupported_opcode) ++unsupported;
  for (const auto& ir : decoded) dec::require(arm::reencode_instruction(ir) == ir.encoding, "arm real-binary reencode mismatch");
  dec::require_unsupported_rate("arm_real_binary", unsupported, decoded.size());
}
}  // namespace

int main() {
  try {
    const auto corpus = build_corpus();
    std::cout << "NOTE arm corpus_count=" << corpus.size() << '\n';
    std::uint64_t address = 0x3000;
    for (const auto& tc : corpus) {
      verify_case(tc, address, static_cast<std::size_t>(&tc - corpus.data()));
      address += tc.bytes.size();
    }
    run_real_binary_probe();
    std::cout << "arm_decoder_corpus OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "arm_decoder_corpus failed: " << ex.what() << '\n';
    return 1;
  }
}
