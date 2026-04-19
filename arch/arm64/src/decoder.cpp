#include <vmp/arch/arm64/ir.h>

#include <array>
#include <cstdint>
#include <sstream>
#include <stdexcept>

namespace vmp::arch::arm64 {
namespace common = vmp::arch::common;
namespace {

std::uint32_t read_u32le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  if (offset + 4 > bytes.size()) throw std::runtime_error("arm64 decode: truncated instruction");
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
}

std::string xreg(std::uint8_t reg, bool is64 = true) {
  if (reg == 31u) return is64 ? "sp" : "wsp";
  std::ostringstream oss;
  oss << (is64 ? 'x' : 'w') << static_cast<unsigned>(reg);
  return oss.str();
}

std::string dreg(std::uint8_t reg) {
  std::ostringstream oss; oss << 'd' << static_cast<unsigned>(reg); return oss.str();
}
std::string vreg(std::uint8_t reg) {
  std::ostringstream oss; oss << 'v' << static_cast<unsigned>(reg); return oss.str();
}

void push_operand(InstructionIR& ir, const std::string& text, OperandKind kind, std::uint8_t size, std::uint64_t imm = 0) {
  ir.operands.push_back(text);
  ir.operand_kinds.push_back(kind);
  ir.operand_sizes.push_back(size);
  if (kind == OperandKind::imm) ir.immediate_values.push_back(imm);
}

std::string mem_text(const MemoryAddress& mem) {
  std::ostringstream oss;
  if (mem.pre_index) oss << '[';
  else oss << '[';
  oss << xreg(mem.base);
  if (mem.index != 0xFFu) {
    oss << ", " << xreg(mem.index);
    if (mem.scale != 1u) oss << ", lsl #" << static_cast<unsigned>(mem.scale);
  } else if (mem.displacement != 0) {
    oss << ", #" << mem.displacement;
  }
  oss << ']';
  if (mem.post_index) oss << ", #" << mem.displacement;
  else if (mem.pre_index) oss << '!';
  return oss.str();
}

InstructionIR decode_impl(const std::vector<std::uint8_t>& bytes, std::uint64_t address) {
  if (bytes.size() < 4) throw std::runtime_error("arm64 decode: short input");
  const auto word = read_u32le(bytes, 0);
  InstructionIR ir;
  ir.address = address;
  ir.size = 4;
  ir.encoding.assign(bytes.begin(), bytes.begin() + 4);
  ir.can_reencode = true;
  const auto rd = static_cast<std::uint8_t>(word & 0x1Fu);
  const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1Fu);
  const auto rm = static_cast<std::uint8_t>((word >> 16) & 0x1Fu);
  const bool is64 = ((word >> 31) & 1u) != 0u;

  auto set_rel = [&](std::int64_t rel) {
    ir.relative_target = static_cast<std::uint64_t>(static_cast<std::int64_t>(address) + rel);
    ir.has_relative_target = true;
    push_operand(ir, std::to_string(ir.relative_target), OperandKind::label, 8, ir.relative_target);
  };

  if ((word & 0x1F000000u) == 0x10000000u) {
    ir.mnemonic = ((word >> 31) & 1u) ? "adrp" : "adr";
    std::int64_t imm = ((static_cast<std::int64_t>((word >> 5) & 0x7FFFFu) << 2) | ((word >> 29) & 0x3u));
    if (imm & (1ll << 20)) imm |= ~((1ll << 21) - 1);
    push_operand(ir, xreg(rd), OperandKind::reg, is64 ? 8 : 4);
    set_rel(ir.mnemonic == "adrp" ? (imm << 12) : imm);
  } else if ((word & 0x7C000000u) == 0x14000000u) {
    ir.mnemonic = ((word >> 31) & 1u) ? "bl" : "b";
    std::int64_t imm = static_cast<std::int32_t>((word & 0x03FFFFFFu) << 6) >> 4;
    set_rel(imm);
  } else if ((word & 0xFF000010u) == 0x54000000u) {
    ir.mnemonic = "b.cond";
    ir.condition = static_cast<ConditionCode>(word & 0xFu);
    std::int64_t imm = static_cast<std::int32_t>(((word >> 5) & 0x7FFFFu) << 13) >> 11;
    set_rel(imm);
  } else if ((word & 0x7E000000u) == 0x34000000u) {
    ir.mnemonic = ((word >> 24) & 1u) ? "cbnz" : "cbz";
    push_operand(ir, xreg(rd, is64), OperandKind::reg, is64 ? 8 : 4);
    std::int64_t imm = static_cast<std::int32_t>(((word >> 5) & 0x7FFFFu) << 13) >> 11;
    set_rel(imm);
  } else if ((word & 0x7E000000u) == 0x36000000u) {
    ir.mnemonic = ((word >> 24) & 1u) ? "tbnz" : "tbz";
    push_operand(ir, xreg(rd), OperandKind::reg, is64 ? 8 : 4);
    push_operand(ir, std::to_string((word >> 19) & 0x3Fu), OperandKind::imm, 1, (word >> 19) & 0x3Fu);
    std::int64_t imm = static_cast<std::int32_t>(((word >> 5) & 0x3FFFu) << 18) >> 16;
    set_rel(imm);
  } else if ((word & 0xFFFFFC1Fu) == 0xD65F0000u) {
    ir.mnemonic = "ret";
    push_operand(ir, xreg(rn), OperandKind::reg, 8);
  } else if ((word & 0xFFFFFC1Fu) == 0xD61F0000u) {
    ir.mnemonic = "br";
    push_operand(ir, xreg(rn), OperandKind::reg, 8);
  } else if ((word & 0xFFFFFC1Fu) == 0xD63F0000u) {
    ir.mnemonic = "blr";
    push_operand(ir, xreg(rn), OperandKind::reg, 8);
  } else if ((word & 0x1F000000u) == 0x11000000u) {
    ir.mnemonic = (word & 0x40000000u) ? "sub" : "add";
    const auto imm = (word >> 10) & 0xFFFu;
    push_operand(ir, xreg(rd, is64), OperandKind::reg, is64 ? 8 : 4);
    push_operand(ir, xreg(rn, is64), OperandKind::reg, is64 ? 8 : 4);
    push_operand(ir, std::to_string(imm), OperandKind::imm, 4, imm);
  } else if ((word & 0x1F200000u) == 0x0B000000u || (word & 0x1F200000u) == 0x4B000000u) {
    ir.mnemonic = ((word & 0x40000000u) != 0u) ? "sub" : "add";
    push_operand(ir, xreg(rd, is64), OperandKind::reg, is64 ? 8 : 4);
    push_operand(ir, xreg(rn, is64), OperandKind::reg, is64 ? 8 : 4);
    push_operand(ir, xreg(rm, is64), OperandKind::reg, is64 ? 8 : 4);
  } else if ((word & 0x1F200000u) == 0x0A000000u || (word & 0x1F200000u) == 0x2A000000u || (word & 0x1F200000u) == 0x4A000000u) {
    ir.mnemonic = (word & 0x40000000u) ? "eor" : ((word & 0x20000000u) ? "orr" : "and");
    push_operand(ir, xreg(rd, is64), OperandKind::reg, is64 ? 8 : 4);
    push_operand(ir, xreg(rn, is64), OperandKind::reg, is64 ? 8 : 4);
    push_operand(ir, xreg(rm, is64), OperandKind::reg, is64 ? 8 : 4);
  } else if ((word & 0x1FE00000u) == 0x1A800000u) {
    ir.mnemonic = (word & 0x1000u) ? "csinv" : "csel";
    ir.condition = static_cast<ConditionCode>((word >> 12) & 0xFu);
    push_operand(ir, xreg(rd, is64), OperandKind::reg, is64 ? 8 : 4);
    push_operand(ir, xreg(rn, is64), OperandKind::reg, is64 ? 8 : 4);
    push_operand(ir, xreg(rm, is64), OperandKind::reg, is64 ? 8 : 4);
  } else if ((word & 0x7F800000u) == 0x52800000u || (word & 0x7F800000u) == 0x72800000u || (word & 0x7F800000u) == 0x12800000u) {
    ir.mnemonic = ((word & 0x60000000u) == 0x00000000u) ? "movn" : ((word & 0x60000000u) == 0x60000000u ? "movk" : "movz");
    const auto imm = ((word >> 5) & 0xFFFFu) << (((word >> 21) & 0x3u) * 16u);
    push_operand(ir, xreg(rd, is64), OperandKind::reg, is64 ? 8 : 4);
    push_operand(ir, std::to_string(imm), OperandKind::imm, 8, imm);
  } else if ((word & 0x3B200000u) == 0x38000000u) {
    const auto size_code = static_cast<std::uint8_t>((word >> 30) & 0x3u);
    const auto access_width = static_cast<std::uint8_t>(size_code == 0 ? 1u : size_code == 1 ? 2u : size_code == 2 ? 4u : 8u);
    const auto imm9 = static_cast<std::int32_t>((word >> 12) & 0x1FFu);
    ir.mnemonic = ((word >> 22) & 1u) ? "ldur" : "stur";
    ir.memory.valid = true;
    ir.memory.base = rn;
    ir.memory.displacement = (imm9 & 0x100) ? (imm9 | ~0x1FF) : imm9;
    if (ir.mnemonic == "ldur") push_operand(ir, xreg(rd, access_width == 8), OperandKind::reg, access_width);
    push_operand(ir, mem_text(ir.memory), OperandKind::mem, access_width);
    if (ir.mnemonic == "stur") {
      ir.operands.insert(ir.operands.begin(), xreg(rd, access_width == 8));
      ir.operand_kinds.insert(ir.operand_kinds.begin(), OperandKind::reg);
      ir.operand_sizes.insert(ir.operand_sizes.begin(), access_width);
    }
  } else if ((word & 0x3B000000u) == 0x39000000u) {
    ir.mnemonic = ((word >> 22) & 1u) ? "ldr" : "str";
    ir.memory.valid = true;
    ir.memory.base = rn;
    ir.memory.displacement = static_cast<std::int64_t>((word >> 10) & 0xFFFu) * (((word >> 30) & 0x3u) == 3 ? 8 : 4);
    if (ir.mnemonic == "ldr") push_operand(ir, xreg(rd, is64), OperandKind::reg, is64 ? 8 : 4);
    push_operand(ir, mem_text(ir.memory), OperandKind::mem, is64 ? 8 : 4);
    if (ir.mnemonic == "str") { ir.operands.insert(ir.operands.begin(), xreg(rd, is64)); ir.operand_kinds.insert(ir.operand_kinds.begin(), OperandKind::reg); ir.operand_sizes.insert(ir.operand_sizes.begin(), is64 ? 8 : 4); }
  } else if ((word & 0x3B200000u) == 0x29000000u) {
    ir.mnemonic = ((word >> 22) & 1u) ? "ldp" : "stp";
    ir.memory.valid = true;
    ir.memory.base = rn;
    ir.memory.displacement = static_cast<std::int64_t>(static_cast<std::int32_t>(((word >> 15) & 0x7Fu) << 25) >> 25) * (is64 ? 8 : 4);
    ir.memory.pre_index = ((word >> 23) & 0x3u) == 3u;
    ir.memory.post_index = ((word >> 23) & 0x3u) == 1u;
    push_operand(ir, xreg(rd, is64), OperandKind::reg, is64 ? 8 : 4);
    push_operand(ir, xreg(rm, is64), OperandKind::reg, is64 ? 8 : 4);
    push_operand(ir, mem_text(ir.memory), OperandKind::mem, is64 ? 8 : 4);
  } else if ((word & 0xFFE0FC00u) == 0x9B007C00u) {
    ir.mnemonic = "mul";
    push_operand(ir, xreg(rd, is64), OperandKind::reg, is64 ? 8 : 4);
    push_operand(ir, xreg(rn, is64), OperandKind::reg, is64 ? 8 : 4);
    push_operand(ir, xreg(rm, is64), OperandKind::reg, is64 ? 8 : 4);
  } else if ((word & 0xFFE0FC00u) == 0x9AC00C00u || (word & 0xFFE0FC00u) == 0x9AC00800u) {
    ir.mnemonic = ((word & 0x400u) != 0u) ? "sdiv" : "udiv";
    push_operand(ir, xreg(rd, is64), OperandKind::reg, is64 ? 8 : 4);
    push_operand(ir, xreg(rn, is64), OperandKind::reg, is64 ? 8 : 4);
    push_operand(ir, xreg(rm, is64), OperandKind::reg, is64 ? 8 : 4);
  } else if ((word & 0xFFFFFC1Fu) == 0xD503201Fu) {
    ir.mnemonic = "nop";
  } else if ((word & 0xFFE0001Fu) == 0xD4000001u) {
    ir.mnemonic = "svc";
    push_operand(ir, std::to_string((word >> 5) & 0xFFFFu), OperandKind::imm, 4, (word >> 5) & 0xFFFFu);
  } else if ((word & 0xFFE0001Fu) == 0xD4200000u) {
    ir.mnemonic = "brk";
    push_operand(ir, std::to_string((word >> 5) & 0xFFFFu), OperandKind::imm, 4, (word >> 5) & 0xFFFFu);
  } else if (word == 0xD503309Fu) {
    ir.mnemonic = "wfe";
  } else if (word == 0xD50330BFu) {
    ir.mnemonic = "wfi";
  } else if (word == 0xD5033BBFu) {
    ir.mnemonic = "dmb";
  } else if (word == 0xD5033F9Fu) {
    ir.mnemonic = "dsb";
  } else if (word == 0xD5033FDFu) {
    ir.mnemonic = "isb";
  } else {
    ir.mnemonic = "opaque";
    ir.can_reencode = false;
    ir.skip_reason = "SKIP_REASON=arm64 decode-only fallback retained original bytes.";
  }
  return ir;
}
}  // namespace

InstructionIR decode_instruction(const std::vector<std::uint8_t>& bytes, std::uint64_t address) {
  return decode_impl(bytes, address);
}

std::vector<InstructionIR> decode_stream(const std::vector<std::uint8_t>& bytes,
                                         std::uint64_t base_address,
                                         std::vector<common::Diagnostic>* diagnostics) {
  std::vector<InstructionIR> out;
  for (std::size_t offset = 0; offset + 4 <= bytes.size(); offset += 4) {
    std::vector<std::uint8_t> slice(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.begin() + static_cast<std::ptrdiff_t>(offset + 4));
    auto ir = decode_instruction(slice, base_address + offset);
    ir.offset = offset;
    if (ir.mnemonic == "opaque" && diagnostics != nullptr) diagnostics->push_back({common::DiagnosticKind::unsupported_opcode, offset, ir.skip_reason});
    out.push_back(ir);
  }
  return out;
}

std::vector<std::uint8_t> reencode_instruction(const InstructionIR& instruction) {
  if (instruction.encoding.empty()) throw std::runtime_error("arm64 reencode: no encoding");
  return instruction.encoding;
}

}  // namespace vmp::arch::arm64
