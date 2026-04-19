#include "decoder_test_common.h"

#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <vector>

#include <vmp/arch/arm64/arm64.h>
#include <vmp/arch/arm64/ir.h>

namespace dec = vmp::tests::arch::decoder;
namespace arm64 = vmp::arch::arm64;
namespace common = vmp::arch::common;

namespace {
struct Case {
  std::uint32_t word = 0;
  std::string mnemonic;
  std::optional<arm64::ConditionCode> cond;
  bool expect_mem = false;
  bool expect_rel = false;
};

std::vector<std::uint8_t> le32(std::uint32_t word) {
  return {static_cast<std::uint8_t>(word & 0xFFu), static_cast<std::uint8_t>((word >> 8) & 0xFFu),
          static_cast<std::uint8_t>((word >> 16) & 0xFFu), static_cast<std::uint8_t>((word >> 24) & 0xFFu)};
}

std::uint32_t enc_addsub_imm(bool is64, bool sub, std::uint8_t rd, std::uint8_t rn, std::uint16_t imm12) {
  return (is64 ? 0x80000000u : 0u) | (sub ? 0x40000000u : 0u) | 0x11000000u |
         ((static_cast<std::uint32_t>(imm12) & 0xFFFu) << 10) | (static_cast<std::uint32_t>(rn) << 5) | rd;
}

std::uint32_t enc_addsub_reg(bool is64, bool sub, std::uint8_t rd, std::uint8_t rn, std::uint8_t rm) {
  return (is64 ? 0x80000000u : 0u) | (sub ? 0x40000000u : 0u) | 0x0B000000u |
         (static_cast<std::uint32_t>(rm) << 16) | (static_cast<std::uint32_t>(rn) << 5) | rd;
}

std::uint32_t enc_logic_reg(bool is64, std::uint32_t base, std::uint8_t rd, std::uint8_t rn, std::uint8_t rm) {
  return (is64 ? 0x80000000u : 0u) | base | (static_cast<std::uint32_t>(rm) << 16) |
         (static_cast<std::uint32_t>(rn) << 5) | rd;
}

std::uint32_t enc_movwide(std::uint32_t base, bool is64, std::uint8_t rd, std::uint16_t imm16, std::uint8_t hw = 0) {
  return base | (is64 ? 0x80000000u : 0u) | (static_cast<std::uint32_t>(hw & 3u) << 21) |
         (static_cast<std::uint32_t>(imm16) << 5) | rd;
}

std::uint32_t enc_branch(bool link, std::int32_t imm26_words) {
  return (link ? 0x94000000u : 0x14000000u) | (static_cast<std::uint32_t>(imm26_words) & 0x03FFFFFFu);
}

std::uint32_t enc_bcond(arm64::ConditionCode cc, std::int32_t imm19_words) {
  return 0x54000000u | ((static_cast<std::uint32_t>(imm19_words) & 0x7FFFFu) << 5) | static_cast<std::uint32_t>(cc);
}

std::uint32_t enc_cbz(bool nonzero, std::uint8_t rt, std::int32_t imm19_words) {
  return (nonzero ? 0x35000000u : 0x34000000u) | ((static_cast<std::uint32_t>(imm19_words) & 0x7FFFFu) << 5) | rt;
}

std::uint32_t enc_tbz(bool nonzero, std::uint8_t rt, std::uint8_t bit, std::int32_t imm14_words) {
  return (nonzero ? 0x37000000u : 0x36000000u) | ((static_cast<std::uint32_t>(bit & 0x3Fu)) << 19) |
         ((static_cast<std::uint32_t>(imm14_words) & 0x3FFFu) << 5) | rt;
}

std::uint32_t enc_ret(std::uint8_t rn) { return 0xD65F0000u | (static_cast<std::uint32_t>(rn) << 5); }
std::uint32_t enc_br(std::uint8_t rn) { return 0xD61F0000u | (static_cast<std::uint32_t>(rn) << 5); }
std::uint32_t enc_blr(std::uint8_t rn) { return 0xD63F0000u | (static_cast<std::uint32_t>(rn) << 5); }

std::uint32_t enc_ldrstr(bool load, bool is64, std::uint8_t rt, std::uint8_t rn, std::uint16_t imm12) {
  const std::uint32_t base = is64 ? (load ? 0xF9400000u : 0xF9000000u) : (load ? 0xB9400000u : 0xB9000000u);
  return base | ((static_cast<std::uint32_t>(imm12) & 0xFFFu) << 10) | (static_cast<std::uint32_t>(rn) << 5) | rt;
}

std::uint32_t enc_ldpstp(bool load, bool is64, std::uint8_t rt, std::uint8_t rt2, std::uint8_t rn, std::int8_t imm7) {
  const std::uint32_t base = is64 ? (load ? 0xA9400000u : 0xA9000000u) : (load ? 0x29400000u : 0x29000000u);
  return base | ((static_cast<std::uint32_t>(imm7) & 0x7Fu) << 15) | (static_cast<std::uint32_t>(rt2) << 10) |
         (static_cast<std::uint32_t>(rn) << 5) | rt;
}

std::uint32_t enc_mul(std::uint8_t rd, std::uint8_t rn, std::uint8_t rm) {
  return 0x9B007C00u | (static_cast<std::uint32_t>(rm) << 16) | (static_cast<std::uint32_t>(rn) << 5) | rd;
}

std::uint32_t enc_div(bool signed_div, std::uint8_t rd, std::uint8_t rn, std::uint8_t rm) {
  return (signed_div ? 0x9AC00C00u : 0x9AC00800u) | (static_cast<std::uint32_t>(rm) << 16) |
         (static_cast<std::uint32_t>(rn) << 5) | rd;
}

std::uint32_t enc_csel(bool csinv, std::uint8_t rd, std::uint8_t rn, std::uint8_t rm, arm64::ConditionCode cc) {
  return 0x9A800000u | (csinv ? 0x1000u : 0u) | (static_cast<std::uint32_t>(rm) << 16) |
         (static_cast<std::uint32_t>(cc) << 12) | (static_cast<std::uint32_t>(rn) << 5) | rd;
}

std::uint32_t enc_adr(bool page, std::uint8_t rd, std::int32_t imm21) {
  return (page ? 0x90000000u : 0x10000000u) | ((static_cast<std::uint32_t>(imm21) & 0x3u) << 29) |
         (((static_cast<std::uint32_t>(imm21) >> 2) & 0x7FFFFu) << 5) | rd;
}

void verify_case(const Case& tc, std::uint64_t address, std::size_t index) {
  auto ir = arm64::decode_instruction(le32(tc.word), address);
  if (ir.mnemonic != tc.mnemonic) {
    std::ostringstream detail;
    detail << "arm64 mnemonic mismatch case=" << index << " expected=" << tc.mnemonic << " actual=" << ir.mnemonic;
    throw std::runtime_error(detail.str());
  }
  dec::require(ir.mnemonic == tc.mnemonic, "arm64 mnemonic mismatch");
  dec::require(ir.size == 4, "arm64 size mismatch");
  if (tc.cond.has_value()) dec::require(ir.condition == *tc.cond, "arm64 condition mismatch");
  if (tc.expect_mem && !ir.memory.valid) { std::ostringstream detail; detail << "arm64 expected memory operand case=" << index << " mnemonic=" << ir.mnemonic << " word=0x" << std::hex << tc.word; throw std::runtime_error(detail.str()); }
  if (tc.expect_rel && !ir.has_relative_target) { std::ostringstream detail; detail << "arm64 expected relative target case=" << index << " mnemonic=" << ir.mnemonic << " word=0x" << std::hex << tc.word; throw std::runtime_error(detail.str()); }
  dec::require(arm64::reencode_instruction(ir) == le32(tc.word), "arm64 reencode mismatch");
}

std::vector<Case> build_corpus() {
  std::vector<Case> cases;
  auto add = [&](std::uint32_t word, std::string mnemonic, std::optional<arm64::ConditionCode> cond = std::nullopt,
                 bool expect_mem = false, bool expect_rel = false) {
    cases.push_back(Case{word, std::move(mnemonic), cond, expect_mem, expect_rel});
  };

  for (std::uint8_t rd = 0; rd < 6; ++rd) {
    for (std::uint8_t imm = 1; imm <= 5; ++imm) {
      add(enc_addsub_imm(true, false, rd, static_cast<std::uint8_t>((rd + 1) & 31u), imm), "add");
      add(enc_addsub_imm(true, true, rd, static_cast<std::uint8_t>((rd + 2) & 31u), imm + 1), "sub");
      add(enc_addsub_imm(false, false, rd, static_cast<std::uint8_t>((rd + 3) & 31u), imm + 2), "add");
      add(enc_addsub_imm(false, true, rd, static_cast<std::uint8_t>((rd + 4) & 31u), imm + 3), "sub");
    }
  }
  for (std::uint8_t rd = 0; rd < 8; ++rd) {
    add(enc_addsub_reg(true, false, rd, static_cast<std::uint8_t>((rd + 1) & 31u), static_cast<std::uint8_t>((rd + 2) & 31u)), "add");
    add(enc_addsub_reg(true, true, rd, static_cast<std::uint8_t>((rd + 3) & 31u), static_cast<std::uint8_t>((rd + 4) & 31u)), "sub");
    add(enc_logic_reg(true, 0x0A000000u, rd, static_cast<std::uint8_t>((rd + 1) & 31u), static_cast<std::uint8_t>((rd + 2) & 31u)), "and");
    add(enc_logic_reg(true, 0x2A000000u, rd, static_cast<std::uint8_t>((rd + 3) & 31u), static_cast<std::uint8_t>((rd + 4) & 31u)), "orr");
    add(enc_logic_reg(true, 0x4A000000u, rd, static_cast<std::uint8_t>((rd + 5) & 31u), static_cast<std::uint8_t>((rd + 6) & 31u)), "eor");
  }
  for (std::uint8_t rd = 0; rd < 8; ++rd) {
    add(enc_movwide(0x52800000u, true, rd, static_cast<std::uint16_t>(0x10 + rd), 0), "movz");
    add(enc_movwide(0x72800000u, true, rd, static_cast<std::uint16_t>(0x20 + rd), 1), "movk");
    add(enc_movwide(0x12800000u, true, rd, static_cast<std::uint16_t>(0x30 + rd), 0), "movn");
  }
  add(enc_branch(false, 4), "b", std::nullopt, false, true);
  add(enc_branch(true, 4), "bl", std::nullopt, false, true);
  for (std::uint8_t cc = 0; cc < 14; ++cc) add(enc_bcond(static_cast<arm64::ConditionCode>(cc), 3), "b.cond", static_cast<arm64::ConditionCode>(cc), false, true);
  for (std::uint8_t rt = 0; rt < 8; ++rt) {
    add(enc_cbz(false, rt, 2), "cbz", std::nullopt, false, true);
    add(enc_cbz(true, rt, 2), "cbnz", std::nullopt, false, true);
    add(enc_tbz(false, rt, static_cast<std::uint8_t>(rt + 1), 2), "tbz", std::nullopt, false, true);
    add(enc_tbz(true, rt, static_cast<std::uint8_t>(rt + 2), 2), "tbnz", std::nullopt, false, true);
  }
  for (std::uint8_t rn = 0; rn < 8; ++rn) {
    add(enc_ret(rn), "ret");
    add(enc_br(rn), "br");
    add(enc_blr(rn), "blr");
  }
  for (std::uint8_t rd = 0; rd < 8; ++rd) {
    add(enc_ldrstr(true, true, rd, static_cast<std::uint8_t>((rd + 1) & 31u), rd + 1), "ldr", std::nullopt, true);
    add(enc_ldrstr(false, true, rd, static_cast<std::uint8_t>((rd + 2) & 31u), rd + 2), "str", std::nullopt, true);
    add(enc_ldrstr(true, false, rd, static_cast<std::uint8_t>((rd + 3) & 31u), rd + 3), "ldr", std::nullopt, true);
    add(enc_ldrstr(false, false, rd, static_cast<std::uint8_t>((rd + 4) & 31u), rd + 4), "str", std::nullopt, true);
    add(enc_ldpstp(true, true, rd, static_cast<std::uint8_t>((rd + 1) & 31u), static_cast<std::uint8_t>((rd + 2) & 31u), 1), "ldp", std::nullopt, true);
    add(enc_ldpstp(false, true, rd, static_cast<std::uint8_t>((rd + 1) & 31u), static_cast<std::uint8_t>((rd + 2) & 31u), 1), "stp", std::nullopt, true);
  }
  for (std::uint8_t rd = 0; rd < 8; ++rd) {
    add(enc_mul(rd, static_cast<std::uint8_t>((rd + 1) & 31u), static_cast<std::uint8_t>((rd + 2) & 31u)), "mul");
    add(enc_div(true, rd, static_cast<std::uint8_t>((rd + 3) & 31u), static_cast<std::uint8_t>((rd + 4) & 31u)), "sdiv");
    add(enc_div(false, rd, static_cast<std::uint8_t>((rd + 5) & 31u), static_cast<std::uint8_t>((rd + 6) & 31u)), "udiv");
    add(enc_csel(false, rd, static_cast<std::uint8_t>((rd + 1) & 31u), static_cast<std::uint8_t>((rd + 2) & 31u), arm64::ConditionCode::eq), "csel", arm64::ConditionCode::eq);
    add(enc_csel(true, rd, static_cast<std::uint8_t>((rd + 3) & 31u), static_cast<std::uint8_t>((rd + 4) & 31u), arm64::ConditionCode::ne), "csinv", arm64::ConditionCode::ne);
  }
  for (std::uint8_t rd = 0; rd < 10; ++rd) {
    add(enc_adr(false, rd, 16 + rd), "adr", std::nullopt, false, true);
    add(enc_adr(true, rd, 8 + rd), "adrp", std::nullopt, false, true);
  }
  add(0xD503201Fu, "nop");
  add(0xD4000021u, "svc");
  add(0xD4200000u | 0x20000u | (1u << 5), "brk");
  add(0xD503309Fu, "wfe");
  add(0xD50330BFu, "wfi");
  add(0xD5033BBFu, "dmb");
  add(0xD5033F9Fu, "dsb");
  add(0xD5033FDFu, "isb");

  dec::require(cases.size() >= 200, "arm64 corpus too small");
  return cases;
}

void run_real_binary_probe() {
  const auto dir = std::filesystem::temp_directory_path() / "vmp_arm64_decoder_probe";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const auto source = dec::write_c_source(dir, "hello_arm64.c", dec::hello_source());
  const auto binary = dir / "hello_arm64.o";
  std::ostringstream cmd;
  cmd << "aarch64-linux-gnu-gcc -O0 -c " << dec::shell_quote(source) << " -o " << dec::shell_quote(binary);
  dec::run_command_checked(cmd.str());
  const auto text = dec::load_text_section(binary, 1024);
  std::vector<common::Diagnostic> diagnostics;
  const auto decoded = arm64::decode_stream(text.bytes, text.address, &diagnostics);
  std::size_t unsupported = 0;
  for (const auto& d : diagnostics) if (d.kind == common::DiagnosticKind::unsupported_opcode) ++unsupported;
  for (const auto& ir : decoded) dec::require(arm64::reencode_instruction(ir) == ir.encoding, "arm64 real-binary reencode mismatch");
  dec::require_unsupported_rate("arm64_real_binary", unsupported, decoded.size());
}
}  // namespace

int main() {
  try {
    const auto corpus = build_corpus();
    std::cout << "NOTE arm64 corpus_count=" << corpus.size() << '\n';
    std::uint64_t address = 0x4000;
    for (const auto& tc : corpus) {
      verify_case(tc, address, static_cast<std::size_t>(&tc - corpus.data()));
      address += 4;
    }
    run_real_binary_probe();
    std::cout << "arm64_decoder_corpus OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "arm64_decoder_corpus failed: " << ex.what() << '\n';
    return 1;
  }
}
