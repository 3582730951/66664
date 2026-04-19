#include "decoder_test_common.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <vector>

#include <vmp/arch/x86/ir.h>
#include <vmp/arch/x86/x86.h>

namespace dec = vmp::tests::arch::decoder;
namespace x86 = vmp::arch::x86;
namespace common = vmp::arch::common;

namespace {
struct Case {
  std::vector<std::uint8_t> bytes;
  std::string mnemonic;
  std::optional<x86::ConditionCode> cond;
  bool expect_mem = false;
  bool expect_rel = false;
};

void verify_case(const Case& tc, std::uint64_t address, std::size_t index) {
  auto ir = x86::decode_instruction(tc.bytes, address);
  if (ir.mnemonic != tc.mnemonic) {
    std::ostringstream detail;
    detail << "x86 mnemonic mismatch case=" << index << " expected=" << tc.mnemonic << " actual=" << ir.mnemonic;
    throw std::runtime_error(detail.str());
  }
  dec::require(ir.mnemonic == tc.mnemonic, "x86 mnemonic mismatch");
  dec::require(ir.size == tc.bytes.size(), "x86 size mismatch");
  if (tc.cond.has_value()) dec::require(ir.condition == *tc.cond, "x86 condition mismatch");
  if (tc.expect_mem) dec::require(ir.memory.valid, "x86 expected memory operand");
  if (tc.expect_rel) dec::require(ir.has_relative_target, "x86 expected relative target");
  if (ir.can_reencode) dec::require(x86::reencode_instruction(ir) == tc.bytes, "x86 reencode mismatch");
  else dec::require(ir.skip_reason.rfind("SKIP_REASON=", 0) == 0, "x86 decode-only case missing SKIP_REASON");
}

std::vector<Case> build_corpus() {
  std::vector<Case> cases;
  auto add = [&](std::vector<std::uint8_t> bytes, std::string mnemonic, std::optional<x86::ConditionCode> cond = std::nullopt,
                 bool expect_mem = false, bool expect_rel = false) {
    cases.push_back(Case{std::move(bytes), std::move(mnemonic), cond, expect_mem, expect_rel});
  };

  for (std::uint8_t reg = 0; reg < 8; ++reg) {
    add({0x89, static_cast<std::uint8_t>(0xC0u | (reg << 3) | reg)}, "mov");
    add({0x8B, static_cast<std::uint8_t>(0xC0u | (reg << 3) | reg)}, "mov");
    add({0x8D, static_cast<std::uint8_t>(0x45u | (reg << 3)), static_cast<std::uint8_t>(reg * 4)}, "lea", std::nullopt, true);
    add({0x8B, static_cast<std::uint8_t>(0x44u | (reg << 3)), 0x24, static_cast<std::uint8_t>(reg * 2)}, "mov", std::nullopt, true);
    add({0x89, static_cast<std::uint8_t>(0x44u | (reg << 3)), 0x24, static_cast<std::uint8_t>(reg * 3)}, "mov", std::nullopt, true);
  }
  for (std::uint8_t sub = 0; sub < 8; ++sub) {
    add({0x83, static_cast<std::uint8_t>(0xC0u | (sub << 3)), static_cast<std::uint8_t>(sub + 1)},
        std::array<const char*, 8>{{"add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"}}[sub]);
  }
  add({0x01, 0xD8}, "add");
  add({0x29, 0xD8}, "sub");
  add({0x21, 0xD8}, "and");
  add({0x09, 0xD8}, "or");
  add({0x31, 0xD8}, "xor");
  add({0x39, 0xD8}, "cmp");
  add({0x85, 0xD8}, "test");
  add({0x05, 0x78, 0x56, 0x34, 0x12}, "add");
  add({0x2D, 0x78, 0x56, 0x34, 0x12}, "sub");
  add({0x25, 0x78, 0x56, 0x34, 0x12}, "and");
  add({0x3D, 0x78, 0x56, 0x34, 0x12}, "cmp");
  for (std::uint8_t reg = 0; reg < 8; ++reg) {
    add({0xC7, 0x44, 0x24, static_cast<std::uint8_t>(reg),
         static_cast<std::uint8_t>(reg), 0x00, 0x00, 0x00}, "mov", std::nullopt, true);
    add({0xC6, 0x44, 0x24, static_cast<std::uint8_t>(reg), static_cast<std::uint8_t>(reg)}, "mov", std::nullopt, true);
  }
  for (std::uint8_t sub = 0; sub < 8; ++sub) {
    add({0xC1, static_cast<std::uint8_t>(0xC0u | (sub << 3)), 0x03},
        std::array<const char*, 8>{{"rol", "ror", "rcl", "rcr", "shl", "shr", "sal", "sar"}}[sub]);
  }
  for (std::uint8_t reg = 0; reg < 8; ++reg) {
    add({static_cast<std::uint8_t>(0x50u + reg)}, "push");
    add({static_cast<std::uint8_t>(0x58u + reg)}, "pop");
    add({static_cast<std::uint8_t>(0xB8u + reg), static_cast<std::uint8_t>(reg), 0x00, 0x00, 0x00}, "mov");
  }
  add({0x68, 0x78, 0x56, 0x34, 0x12}, "push");
  add({0x6A, 0x7F}, "push");
  add({0xE8, 0x10, 0x00, 0x00, 0x00}, "call", std::nullopt, false, true);
  add({0xE9, 0x10, 0x00, 0x00, 0x00}, "jmp", std::nullopt, false, true);
  add({0xEB, 0xF0}, "jmp", std::nullopt, false, true);
  for (std::uint8_t cc = 0; cc < 16; ++cc) {
    add({static_cast<std::uint8_t>(0x70u + cc), 0x02}, "jcc", static_cast<x86::ConditionCode>(cc), false, true);
    add({0x0F, static_cast<std::uint8_t>(0x80u + cc), 0x08, 0x00, 0x00, 0x00}, "jcc", static_cast<x86::ConditionCode>(cc), false, true);
    add({0x0F, static_cast<std::uint8_t>(0x90u + cc), 0xC0}, "setcc", static_cast<x86::ConditionCode>(cc));
    add({0x0F, static_cast<std::uint8_t>(0x40u + cc), 0xC1}, "cmov", static_cast<x86::ConditionCode>(cc));
  }
  add({0x0F, 0xAF, 0xC1}, "imul");
  add({0x69, 0xC1, 0x34, 0x12, 0x00, 0x00}, "imul");
  add({0x6B, 0xC1, 0x7F}, "imul");
  add({0x0F, 0xB6, 0xC1}, "movzx");
  add({0x0F, 0xB7, 0xC1}, "movzx");
  add({0x0F, 0xBE, 0xC1}, "movsx");
  add({0x0F, 0xBF, 0xC1}, "movsx");
  add({0x0F, 0xBC, 0xC1}, "bsf");
  add({0x0F, 0xBD, 0xC1}, "bsr");
  add({0x9C}, "pushfd");
  add({0x9D}, "popfd");
  add({0xC9}, "leave");
  add({0xC3}, "ret");
  add({0x90}, "nop");
  add({0x0F, 0x1F, 0x40, 0x00}, "nop", std::nullopt, true);
  add({0x0F, 0x28, 0xC1}, "movaps");
  add({0x66, 0x0F, 0x28, 0xC1}, "movapd");
  add({0x0F, 0x58, 0xC1}, "addps");
  add({0x66, 0x0F, 0x58, 0xC1}, "addpd");
  add({0x0F, 0x59, 0xC1}, "mulps");
  add({0x0F, 0x5C, 0xC1}, "subps");
  add({0x0F, 0x5E, 0xC1}, "divps");
  add({0x66, 0x0F, 0x6F, 0xC1}, "movdqa");
  add({0xF3, 0x0F, 0x6F, 0xC1}, "movdqu");
  add({0x66, 0x0F, 0x70, 0xC1, 0x1B}, "pshufd");
  add({0x66, 0x0F, 0x74, 0xC1}, "pcmpeqb");
  add({0x66, 0x0F, 0x75, 0xC1}, "pcmpeqw");
  add({0x66, 0x0F, 0x76, 0xC1}, "pcmpeqd");
  add({0x66, 0x0F, 0x38, 0x00, 0xC1}, "pshufb");

  while (cases.size() < 200) {
    const std::uint8_t reg = static_cast<std::uint8_t>(cases.size() & 7u);
    add({0x8B, static_cast<std::uint8_t>(0x40u | (reg << 3)), static_cast<std::uint8_t>(reg * 7)}, "mov", std::nullopt, true);
  }
  return cases;
}

void run_real_binary_probe() {
  const auto dir = std::filesystem::temp_directory_path() / "vmp_x86_decoder_probe";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const auto binary = dec::compile_c_binary(dir, "gcc", {"-m32", "-O0", "-fno-pic", "-no-pie"}, dec::hello_source(), "hello_x86");
  const auto text = dec::load_text_section(binary, 1024);
  std::vector<common::Diagnostic> diagnostics;
  const auto decoded = x86::decode_stream(text.bytes, text.address, &diagnostics);
  std::size_t unsupported = 0;
  for (const auto& d : diagnostics) if (d.kind == common::DiagnosticKind::unsupported_opcode) ++unsupported;
  for (const auto& ir : decoded) if (ir.can_reencode) dec::require(x86::reencode_instruction(ir) == ir.encoding, "x86 real-binary reencode mismatch");
  dec::require_unsupported_rate("x86_real_binary", unsupported, decoded.size());
}
}  // namespace

int main() {
  try {
    const auto corpus = build_corpus();
    std::cout << "NOTE x86 corpus_count=" << corpus.size() << '\n';
    std::uint64_t address = 0x2000;
    for (const auto& tc : corpus) {
      verify_case(tc, address, static_cast<std::size_t>(&tc - corpus.data()));
      address += 0x20;
    }
    run_real_binary_probe();
    std::cout << "x86_decoder_corpus OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "x86_decoder_corpus failed: " << ex.what() << '\n';
    return 1;
  }
}
