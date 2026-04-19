#include "decoder_test_common.h"

#include <array>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <vector>

#include <vmp/arch/x64/ir.h>
#include <vmp/arch/x64/x64.h>

namespace dec = vmp::tests::arch::decoder;
namespace x64 = vmp::arch::x64;
namespace common = vmp::arch::common;

namespace {
std::string bytes_hex(const std::vector<std::uint8_t>& bytes) {
  std::ostringstream oss;
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (i) oss << " ";
    oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i]);
  }
  return oss.str();
}

struct Case {
  std::vector<std::uint8_t> bytes;
  std::string mnemonic;
  std::optional<x64::ConditionCode> cond;
  bool expect_mem = false;
  bool expect_rel = false;
  std::uint32_t required_flags = 0;
};

void verify_case(const Case& tc, std::uint64_t address, std::size_t index) {
  auto ir = x64::decode_instruction(tc.bytes, address);
  if (ir.mnemonic != tc.mnemonic) {
    std::ostringstream detail;
    detail << "x64 mnemonic mismatch case=" << index << " expected=" << tc.mnemonic << " actual=" << ir.mnemonic;
    throw std::runtime_error(detail.str());
  }
  dec::require(ir.mnemonic == tc.mnemonic, "x64 mnemonic mismatch for corpus case");
  if (ir.size != tc.bytes.size()) { std::ostringstream detail; detail << "x64 size mismatch case=" << index << " expected=" << tc.bytes.size() << " actual=" << ir.size << " bytes=" << bytes_hex(tc.bytes); throw std::runtime_error(detail.str()); }
  if (tc.cond.has_value() && ir.condition != *tc.cond) { std::ostringstream detail; detail << "x64 condition mismatch case=" << index << " bytes=" << bytes_hex(tc.bytes); throw std::runtime_error(detail.str()); }
  if (tc.expect_mem && !ir.memory.valid) { std::ostringstream detail; detail << "x64 expected memory operand case=" << index << " mnemonic=" << ir.mnemonic << " bytes=" << bytes_hex(tc.bytes); throw std::runtime_error(detail.str()); }
  if (tc.expect_rel && !ir.has_relative_target) { std::ostringstream detail; detail << "x64 expected relative target case=" << index << " mnemonic=" << ir.mnemonic << " bytes=" << bytes_hex(tc.bytes); throw std::runtime_error(detail.str()); }
  if (tc.required_flags != 0 && (ir.flags & tc.required_flags) != tc.required_flags) { std::ostringstream detail; detail << "x64 prefix flags missing case=" << index << " bytes=" << bytes_hex(tc.bytes); throw std::runtime_error(detail.str()); }
  if (ir.can_reencode) {
    if (x64::reencode_instruction(ir) != tc.bytes) { std::ostringstream detail; detail << "x64 reencode mismatch case=" << index << " bytes=" << bytes_hex(tc.bytes); throw std::runtime_error(detail.str()); }
  } else {
    if (ir.skip_reason.rfind("SKIP_REASON=", 0) != 0) { std::ostringstream detail; detail << "x64 decode-only case missing SKIP_REASON case=" << index << " bytes=" << bytes_hex(tc.bytes); throw std::runtime_error(detail.str()); }
  }
}

std::vector<Case> build_corpus() {
  std::vector<Case> cases;
  auto add = [&](std::vector<std::uint8_t> bytes, std::string mnemonic, std::optional<x64::ConditionCode> cond = std::nullopt,
                 bool expect_mem = false, bool expect_rel = false, std::uint32_t flags = 0) {
    cases.push_back(Case{std::move(bytes), std::move(mnemonic), cond, expect_mem, expect_rel, flags});
  };

  for (std::uint8_t reg = 0; reg < 16; ++reg) {
    add({0x48, 0x89, static_cast<std::uint8_t>(0xC0u | (reg << 3) | reg)}, "mov");
    add({0x48, 0x8B, static_cast<std::uint8_t>(0xC0u | (reg << 3) | reg)}, "mov");
    add({0x48, 0x8D, static_cast<std::uint8_t>(0x44u | ((reg & 7u) << 3)), 0x24, static_cast<std::uint8_t>(reg)}, "lea", std::nullopt, true);
  }
  for (std::uint8_t reg = 0; reg < 16; ++reg) {
    const auto rex = static_cast<std::uint8_t>(0x48u | ((reg >> 3) & 1u));
    add({rex, 0x8B, static_cast<std::uint8_t>(0x44u | ((reg & 7u) << 3)), 0x24, static_cast<std::uint8_t>(reg * 4)}, "mov", std::nullopt, true);
    add({rex, 0x89, static_cast<std::uint8_t>(0x44u | ((reg & 7u) << 3)), 0x24, static_cast<std::uint8_t>(reg * 3)}, "mov", std::nullopt, true);
    add({rex, 0xC7, 0x44, static_cast<std::uint8_t>(0x20u | (reg & 7u)), static_cast<std::uint8_t>(reg),
         static_cast<std::uint8_t>(reg), 0x00, 0x00, 0x00}, "mov", std::nullopt, true);
  }
  for (std::uint8_t sub = 0; sub < 8; ++sub) {
    add({0x48, 0x83, static_cast<std::uint8_t>(0xC0u | (sub << 3)), static_cast<std::uint8_t>(sub + 1)},
        std::array<const char*, 8>{{"add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"}}[sub]);
  }
  add({0x48, 0x01, 0xD8}, "add");
  add({0x48, 0x29, 0xD8}, "sub");
  add({0x48, 0x21, 0xD8}, "and");
  add({0x48, 0x09, 0xD8}, "or");
  add({0x48, 0x31, 0xD8}, "xor");
  add({0x48, 0x39, 0xD8}, "cmp");
  add({0x48, 0x85, 0xD8}, "test");
  add({0x48, 0x05, 0x78, 0x56, 0x34, 0x12}, "add");
  add({0x48, 0x2D, 0x78, 0x56, 0x34, 0x12}, "sub");
  add({0x48, 0x25, 0x78, 0x56, 0x34, 0x12}, "and");
  add({0x48, 0x3D, 0x78, 0x56, 0x34, 0x12}, "cmp");

  for (std::uint8_t sub = 0; sub < 8; ++sub) {
    add({0x48, 0xC1, static_cast<std::uint8_t>(0xC0u | (sub << 3)), 0x03},
        std::array<const char*, 8>{{"rol", "ror", "rcl", "rcr", "shl", "shr", "sal", "sar"}}[sub]);
  }

  for (std::uint8_t reg = 0; reg < 8; ++reg) {
    add({static_cast<std::uint8_t>(0x50u + reg)}, "push");
    add({static_cast<std::uint8_t>(0x58u + reg)}, "pop");
  }
  for (std::uint8_t reg = 0; reg < 8; ++reg) {
    add({0x41, static_cast<std::uint8_t>(0x50u + reg)}, "push", std::nullopt, false, false, x64::prefix_rex | x64::prefix_rex_b);
    add({0x41, static_cast<std::uint8_t>(0x58u + reg)}, "pop", std::nullopt, false, false, x64::prefix_rex | x64::prefix_rex_b);
  }

  add({0xE8, 0x10, 0x00, 0x00, 0x00}, "call", std::nullopt, false, true);
  add({0xE9, 0x10, 0x00, 0x00, 0x00}, "jmp", std::nullopt, false, true);
  add({0xEB, 0xF0}, "jmp", std::nullopt, false, true);
  for (std::uint8_t cc = 0; cc < 16; ++cc) {
    add({static_cast<std::uint8_t>(0x70u + cc), 0x02}, "jcc", static_cast<x64::ConditionCode>(cc), false, true);
    add({0x0F, static_cast<std::uint8_t>(0x80u + cc), 0x08, 0x00, 0x00, 0x00}, "jcc", static_cast<x64::ConditionCode>(cc), false, true);
    add({0x0F, static_cast<std::uint8_t>(0x90u + cc), 0xC0}, "setcc", static_cast<x64::ConditionCode>(cc));
    add({0x0F, static_cast<std::uint8_t>(0x40u + cc), 0xC1}, "cmov", static_cast<x64::ConditionCode>(cc));
  }

  add({0x0F, 0xB6, 0xC1}, "movzx");
  add({0x0F, 0xB7, 0xC1}, "movzx");
  add({0x0F, 0xBE, 0xC1}, "movsx");
  add({0x0F, 0xBF, 0xC1}, "movsx");
  add({0x0F, 0xBC, 0xC1}, "bsf");
  add({0xF3, 0x0F, 0xBC, 0xC1}, "tzcnt", std::nullopt, false, false, x64::prefix_rep);
  add({0x0F, 0xBD, 0xC1}, "bsr");
  add({0xF3, 0x0F, 0xBD, 0xC1}, "lzcnt", std::nullopt, false, false, x64::prefix_rep);
  add({0x0F, 0xA3, 0xC1}, "bt");
  add({0x0F, 0xAB, 0xC1}, "bts");
  add({0x0F, 0xB3, 0xC1}, "btr");
  add({0x0F, 0xBB, 0xC1}, "btc");
  add({0x48, 0x63, 0xC1}, "movsxd");
  add({0x0F, 0x05}, "syscall");
  add({0x0F, 0x31}, "rdtsc");
  add({0x0F, 0x01, 0xF8}, "swapgs");
  add({0x0F, 0x01, 0xF9}, "rdtscp");
  add({0x0F, 0xA2}, "cpuid");
  add({0x0F, 0x1F, 0x40, 0x00}, "nop", std::nullopt, true);
  add({0xF3, 0x0F, 0x1E, 0xFA}, "endbr64", std::nullopt, false, false, x64::prefix_rep);
  add({0x9C}, "pushf");
  add({0x9D}, "popf");
  add({0xC9}, "leave");

  add({0x0F, 0x28, 0xC1}, "movaps");
  add({0x66, 0x0F, 0x28, 0xC1}, "movapd", std::nullopt, false, false, x64::prefix_66);
  add({0x0F, 0x58, 0xC1}, "addps");
  add({0x66, 0x0F, 0x58, 0xC1}, "addpd", std::nullopt, false, false, x64::prefix_66);
  add({0xF3, 0x0F, 0x58, 0xC1}, "addss", std::nullopt, false, false, x64::prefix_rep);
  add({0xF2, 0x0F, 0x58, 0xC1}, "addsd", std::nullopt, false, false, x64::prefix_repne);
  add({0x66, 0x0F, 0x6F, 0xC1}, "movdqa", std::nullopt, false, false, x64::prefix_66);
  add({0xF3, 0x0F, 0x6F, 0xC1}, "movdqu", std::nullopt, false, false, x64::prefix_rep);
  add({0x66, 0x0F, 0x70, 0xC1, 0x1B}, "pshufd", std::nullopt, false, false, x64::prefix_66);
  add({0x66, 0x0F, 0x74, 0xC1}, "pcmpeqb", std::nullopt, false, false, x64::prefix_66);
  add({0x66, 0x0F, 0x75, 0xC1}, "pcmpeqw", std::nullopt, false, false, x64::prefix_66);
  add({0x66, 0x0F, 0x76, 0xC1}, "pcmpeqd", std::nullopt, false, false, x64::prefix_66);
  add({0x66, 0x0F, 0x38, 0x00, 0xC1}, "pshufb", std::nullopt, false, false, x64::prefix_66);
  add({0x0F, 0x38, 0xF0, 0xC1}, "movbe");
  add({0x0F, 0x38, 0xF1, 0xC1}, "movbe");

  add({0xC5, 0xF8, 0x77}, "vzeroupper", std::nullopt, false, false, x64::prefix_vex);
  add({0xC5, 0xFD, 0x6F, 0xC1}, "vmovdqa", std::nullopt, false, false, x64::prefix_vex);
  add({0xC5, 0xFD, 0xEF, 0xC1}, "vpxor", std::nullopt, false, false, x64::prefix_vex);
  add({0xC4, 0xE2, 0x7D, 0x00, 0xC1}, "vpshufb", std::nullopt, false, false, x64::prefix_vex);
  add({0xC4, 0xE2, 0x79, 0x98, 0xC1}, "vfmadd", std::nullopt, false, false, x64::prefix_vex);
  add({0x62, 0xF1, 0xFD, 0x08, 0xEF, 0xC0}, "vpxorq", std::nullopt, false, false, x64::prefix_evex);

  dec::require(cases.size() >= 200, "x64 corpus too small");
  return cases;
}

void run_real_binary_probe() {
  std::vector<common::Diagnostic> diagnostics;
  const auto text = dec::load_text_section("/usr/bin/ls", 1024);
  const auto decoded = x64::decode_stream(text.bytes, text.address, &diagnostics);
  std::size_t unsupported = 0;
  for (const auto& d : diagnostics) {
    if (d.kind == common::DiagnosticKind::unsupported_opcode) ++unsupported;
  }
  for (const auto& ir : decoded) {
    if (ir.can_reencode) dec::require(x64::reencode_instruction(ir) == ir.encoding, "x64 real-binary reencode mismatch");
  }
  dec::require_unsupported_rate("x64_real_binary", unsupported, decoded.size());
}
}  // namespace

int main() {
  try {
    const auto corpus = build_corpus();
    std::cout << "NOTE x64 corpus_count=" << corpus.size() << '\n';
    std::uint64_t address = 0x1000;
    for (const auto& tc : corpus) {
      verify_case(tc, address, static_cast<std::size_t>(&tc - corpus.data()));
      address += 0x20;
    }
    run_real_binary_probe();
    std::cout << "x64_decoder_corpus OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "x64_decoder_corpus failed: " << ex.what() << '\n';
    return 1;
  }
}
