#include <vmp/arch/arm/ir.h>

#include <cstdint>
#include <sstream>
#include <stdexcept>

namespace vmp::arch::arm {
namespace common = vmp::arch::common;
namespace {
std::uint16_t read_u16le(const std::vector<std::uint8_t>& bytes, std::size_t off) {
  if (off + 2 > bytes.size()) throw std::runtime_error("arm decode: truncated halfword");
  return static_cast<std::uint16_t>(bytes[off]) | static_cast<std::uint16_t>(bytes[off + 1] << 8u);
}
std::uint32_t read_u32le(const std::vector<std::uint8_t>& bytes, std::size_t off) {
  if (off + 4 > bytes.size()) throw std::runtime_error("arm decode: truncated word");
  return static_cast<std::uint32_t>(bytes[off]) | (static_cast<std::uint32_t>(bytes[off + 1]) << 8u) |
         (static_cast<std::uint32_t>(bytes[off + 2]) << 16u) | (static_cast<std::uint32_t>(bytes[off + 3]) << 24u);
}
std::string reg_name(std::uint8_t reg) { std::ostringstream oss; oss << (reg == 13 ? "sp" : reg == 14 ? "lr" : reg == 15 ? "pc" : std::string("r") + std::to_string(reg)); return oss.str(); }
void push_operand(InstructionIR& ir, const std::string& text, OperandKind kind, std::uint8_t size, std::uint64_t imm = 0) {
  ir.operands.push_back(text); ir.operand_kinds.push_back(kind); ir.operand_sizes.push_back(size); if (kind == OperandKind::imm) ir.immediate_values.push_back(imm);
}
std::string mem_text(const MemoryAddress& mem) {
  std::ostringstream oss; oss << '[' << reg_name(mem.base); if (mem.index != 0xFFu) oss << ", " << reg_name(mem.index); else if (mem.displacement != 0) oss << ", #" << mem.displacement; oss << ']'; if (mem.post_index) oss << ", #" << mem.displacement; else if (!mem.pre_index) oss << '!'; return oss.str();
}
InstructionIR decode_arm_word(const std::vector<std::uint8_t>& bytes, std::uint64_t address) {
  const auto w = read_u32le(bytes, 0);
  InstructionIR ir; ir.address = address; ir.size = 4; ir.mode = ExecutionMode::arm; ir.encoding.assign(bytes.begin(), bytes.begin() + 4); ir.can_reencode = true;
  ir.condition = static_cast<ConditionCode>((w >> 28) & 0xFu);
  const auto rd = static_cast<std::uint8_t>((w >> 12) & 0xFu);
  const auto rn = static_cast<std::uint8_t>((w >> 16) & 0xFu);
  const auto rm = static_cast<std::uint8_t>(w & 0xFu);
  if ((w & 0x0E000000u) == 0x0A000000u) {
    ir.mnemonic = ((w >> 24) & 1u) ? "bl" : "b";
    std::int32_t imm24 = w & 0xFFFFFFu; if (imm24 & 0x800000) imm24 |= ~0xFFFFFF;
    ir.relative_target = address + 8 + (static_cast<std::int64_t>(imm24) << 2);
    ir.has_relative_target = true; push_operand(ir, std::to_string(ir.relative_target), OperandKind::label, 4, ir.relative_target);
  } else if ((w & 0x0FFFFFF0u) == 0x012FFF10u) {
    ir.mnemonic = "bx"; push_operand(ir, reg_name(rm), OperandKind::reg, 4);
  } else if ((w & 0x0FFFFFF0u) == 0x012FFF30u) {
    ir.mnemonic = "blx"; push_operand(ir, reg_name(rm), OperandKind::reg, 4);
  } else if ((w & 0x0FC000F0u) == 0x00000090u) {
    ir.mnemonic = ((w >> 21) & 1u) ? "mla" : "mul"; push_operand(ir, reg_name(rd), OperandKind::reg, 4); push_operand(ir, reg_name(rm), OperandKind::reg, 4); push_operand(ir, reg_name(static_cast<std::uint8_t>((w >> 8) & 0xFu)), OperandKind::reg, 4);
  } else if ((w & 0x0FF0F0F0u) == 0x0710F010u || (w & 0x0FF0F0F0u) == 0x0730F010u) {
    const auto div_rd = static_cast<std::uint8_t>((w >> 16) & 0xFu);
    const auto div_rm = static_cast<std::uint8_t>((w >> 8) & 0xFu);
    const auto div_rn = static_cast<std::uint8_t>(w & 0xFu);
    ir.mnemonic = ((w & 0x00200000u) != 0u) ? "udiv" : "sdiv"; push_operand(ir, reg_name(div_rd), OperandKind::reg, 4); push_operand(ir, reg_name(div_rn), OperandKind::reg, 4); push_operand(ir, reg_name(div_rm), OperandKind::reg, 4);
  } else if ((w & 0x0C000000u) == 0x04000000u) {
    ir.mnemonic = ((w >> 20) & 1u) ? "ldr" : "str"; ir.memory.valid = true; ir.memory.base = rn; ir.memory.displacement = w & 0xFFFu; ir.memory.pre_index = ((w >> 24) & 1u) != 0; ir.memory.post_index = ((w >> 24) & 1u) == 0; if ((w & (1u << 25)) != 0u) ir.memory.index = rm; if (ir.mnemonic == "ldr") push_operand(ir, reg_name(rd), OperandKind::reg, 4); push_operand(ir, mem_text(ir.memory), OperandKind::mem, 4); if (ir.mnemonic == "str") { ir.operands.insert(ir.operands.begin(), reg_name(rd)); ir.operand_kinds.insert(ir.operand_kinds.begin(), OperandKind::reg); ir.operand_sizes.insert(ir.operand_sizes.begin(), 4); }
  } else if ((w & 0x0E000000u) == 0x08000000u) {
    ir.mnemonic = ((w >> 20) & 1u) ? "ldm" : "stm"; ir.memory.valid = true; ir.memory.base = rn; push_operand(ir, reg_name(rn), OperandKind::reg, 4); push_operand(ir, std::to_string(w & 0xFFFFu), OperandKind::imm, 4, w & 0xFFFFu);
  } else if ((w & 0x0FB00FF0u) == 0x01000090u) {
    ir.mnemonic = "swp"; ir.memory.valid = true; ir.memory.base = rn; push_operand(ir, reg_name(rd), OperandKind::reg, 4); push_operand(ir, reg_name(rm), OperandKind::reg, 4); push_operand(ir, '[' + reg_name(rn) + ']', OperandKind::mem, 4);
  } else if ((w & 0xFFF0FFF0u) == 0xF3B08F40u) {
    ir.mnemonic = "dsb"; ir.condition = ConditionCode::none;
  } else if ((w & 0xFFF0FFF0u) == 0xF3B08F50u) {
    ir.mnemonic = "dmb"; ir.condition = ConditionCode::none;
  } else if ((w & 0xFFF0FFF0u) == 0xF3B08F60u) {
    ir.mnemonic = "isb"; ir.condition = ConditionCode::none;
  } else if ((w & 0x0FFFFFF0u) == 0x016F0F10u) {
    ir.mnemonic = "clrex"; ir.condition = ConditionCode::none;
  } else if ((w & 0x0FFFFFF0u) == 0x0320F000u) {
    ir.mnemonic = "nop"; ir.condition = ConditionCode::none;
  } else if ((w & 0x0C000000u) == 0x00000000u) {
    const auto opcode = (w >> 21) & 0xFu;
    const bool immediate = ((w >> 25) & 1u) != 0u;
    static const char* kNames[] = {"and","eor","sub","rsb","add","adc","sbc","rsc","tst","teq","cmp","cmn","orr","mov","bic","mvn"};
    ir.mnemonic = kNames[opcode];
    if (opcode == 13 || opcode == 15) {
      push_operand(ir, reg_name(rd), OperandKind::reg, 4);
      if (immediate) push_operand(ir, std::to_string((w & 0xFFu)), OperandKind::imm, 4, w & 0xFFu);
      else push_operand(ir, reg_name(rm), OperandKind::reg, 4);
    } else if (opcode >= 8 && opcode <= 11) {
      push_operand(ir, reg_name(rn), OperandKind::reg, 4);
      if (immediate) push_operand(ir, std::to_string((w & 0xFFu)), OperandKind::imm, 4, w & 0xFFu);
      else push_operand(ir, reg_name(rm), OperandKind::reg, 4);
    } else {
      push_operand(ir, reg_name(rd), OperandKind::reg, 4);
      push_operand(ir, reg_name(rn), OperandKind::reg, 4);
      if (immediate) push_operand(ir, std::to_string((w & 0xFFu)), OperandKind::imm, 4, w & 0xFFu);
      else push_operand(ir, reg_name(rm), OperandKind::reg, 4);
    }
  } else if ((w & 0x0F000FF0u) == 0x0E000A10u) {
    ir.mnemonic = "vmov"; push_operand(ir, std::string("s") + std::to_string(rd), OperandKind::fp_reg, 4); push_operand(ir, reg_name(rm), OperandKind::reg, 4);
  } else if ((w & 0x0FB00F50u) == 0x0E300A00u) {
    ir.mnemonic = "vadd.f32"; push_operand(ir, std::string("s") + std::to_string(rd), OperandKind::fp_reg, 4); push_operand(ir, std::string("s") + std::to_string(rn), OperandKind::fp_reg, 4); push_operand(ir, std::string("s") + std::to_string(rm), OperandKind::fp_reg, 4);
  } else {
    ir.mnemonic = "opaque"; ir.can_reencode = false; ir.skip_reason = "SKIP_REASON=arm32 decode-only fallback retained original bytes.";
  }
  return ir;
}
InstructionIR decode_thumb(const std::vector<std::uint8_t>& bytes, std::uint64_t address) {
  const auto h0 = read_u16le(bytes, 0);
  InstructionIR ir; ir.address = address; ir.mode = ExecutionMode::thumb; ir.can_reencode = true;
  if ((h0 & 0xF800u) == 0x1800u) { ir.size = 2; ir.encoding.assign(bytes.begin(), bytes.begin() + 2); ir.mnemonic = "add"; push_operand(ir, reg_name((h0 & 7u)), OperandKind::reg, 4); push_operand(ir, reg_name((h0 >> 3) & 7u), OperandKind::reg, 4); push_operand(ir, reg_name((h0 >> 6) & 7u), OperandKind::reg, 4); }
  else if ((h0 & 0xF500u) == 0xB100u) { ir.size = 2; ir.encoding.assign(bytes.begin(), bytes.begin() + 2); ir.mnemonic = (h0 & 0x0800u) ? "cbnz" : "cbz"; push_operand(ir, reg_name((h0 >> 0) & 7u), OperandKind::reg, 4); std::int32_t imm = (((h0 >> 3) & 0x1Fu) | ((h0 >> 9) & 0x20u)) << 1; ir.relative_target = address + 4 + imm; ir.has_relative_target = true; push_operand(ir, std::to_string(ir.relative_target), OperandKind::label, 4, ir.relative_target); }
  else if ((h0 & 0xF800u) == 0xE000u) { ir.size = 2; ir.encoding.assign(bytes.begin(), bytes.begin() + 2); ir.mnemonic = "b"; std::int32_t imm = static_cast<std::int16_t>((h0 & 0x7FFu) << 5) >> 4; ir.relative_target = address + 4 + imm; ir.has_relative_target = true; push_operand(ir, std::to_string(ir.relative_target), OperandKind::label, 4, ir.relative_target); }
  else if ((h0 & 0xF800u) == 0x4800u) { ir.size = 2; ir.encoding.assign(bytes.begin(), bytes.begin() + 2); ir.mnemonic = "ldr"; push_operand(ir, reg_name((h0 >> 8) & 7u), OperandKind::reg, 4); push_operand(ir, "[pc,#" + std::to_string((h0 & 0xFFu) << 2) + "]", OperandKind::mem, 4); }
  else { ir.size = ((h0 & 0xE000u) == 0xE000u || (h0 & 0xF800u) == 0xF000u) ? 4 : 2; ir.encoding.assign(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(ir.size)); ir.mnemonic = "opaque"; ir.can_reencode = false; ir.skip_reason = "SKIP_REASON=thumb decode-only fallback retained original bytes."; }
  return ir;
}
}  // namespace

InstructionIR decode_instruction(const std::vector<std::uint8_t>& bytes, std::uint64_t address, ExecutionMode mode) {
  return mode == ExecutionMode::arm ? decode_arm_word(bytes, address) : decode_thumb(bytes, address);
}

std::vector<InstructionIR> decode_stream(const std::vector<std::uint8_t>& bytes,
                                         std::uint64_t base_address,
                                         ExecutionMode mode,
                                         std::vector<common::Diagnostic>* diagnostics) {
  std::vector<InstructionIR> out;
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    auto slice_end = mode == ExecutionMode::arm ? offset + 4 : std::min(bytes.size(), offset + 4);
    std::vector<std::uint8_t> slice(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.begin() + static_cast<std::ptrdiff_t>(slice_end));
    auto ir = decode_instruction(slice, base_address + offset, mode);
    ir.offset = offset;
    if (ir.mnemonic == "opaque" && diagnostics != nullptr) diagnostics->push_back({common::DiagnosticKind::unsupported_opcode, offset, ir.skip_reason});
    out.push_back(ir);
    offset += ir.size == 0 ? (mode == ExecutionMode::arm ? 4 : 2) : ir.size;
  }
  return out;
}

std::vector<std::uint8_t> reencode_instruction(const InstructionIR& instruction) {
  if (instruction.encoding.empty()) throw std::runtime_error("arm reencode: no encoding");
  return instruction.encoding;
}

}  // namespace vmp::arch::arm
