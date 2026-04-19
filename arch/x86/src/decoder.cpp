#include <vmp/arch/x86/ir.h>

#include <array>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace vmp::arch::x86 {
namespace common = vmp::arch::common;
namespace {
struct Cursor {
  const std::vector<std::uint8_t>& bytes;
  std::size_t off = 0;
  bool eof() const noexcept { return off >= bytes.size(); }
  std::uint8_t peek() const { return bytes.at(off); }
  std::uint8_t next() { return bytes.at(off++); }
};

constexpr std::array<const char*, 8> kGpr32{{"eax","ecx","edx","ebx","esp","ebp","esi","edi"}};
constexpr std::array<const char*, 8> kXmm{{"xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7"}};

std::uint64_t read_imm(const std::vector<std::uint8_t>& bytes, std::size_t& off, std::size_t width) {
  if (off + width > bytes.size()) throw std::runtime_error("x86 decode: truncated immediate");
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < width; ++i) value |= static_cast<std::uint64_t>(bytes[off + i]) << (i * 8u);
  off += width;
  return value;
}

std::string reg_name(std::uint8_t reg, std::uint8_t width) {
  static constexpr std::array<const char*, 8> kGpr16{{"ax","cx","dx","bx","sp","bp","si","di"}};
  static constexpr std::array<const char*, 8> kGpr8{{"al","cl","dl","bl","ah","ch","dh","bh"}};
  switch (width) {
    case 1: return kGpr8.at(reg & 7u);
    case 2: return kGpr16.at(reg & 7u);
    default: return kGpr32.at(reg & 7u);
  }
}

void push_operand(InstructionIR& ir, const std::string& text, OperandKind kind, std::uint8_t size, std::uint64_t imm = 0) {
  ir.operands.push_back(text);
  ir.operand_kinds.push_back(kind);
  ir.operand_sizes.push_back(size);
  if (kind == OperandKind::imm) ir.immediate_values.push_back(imm);
}

MemoryAddress decode_memory(Cursor& cur, std::uint8_t mod, std::uint8_t rm) {
  MemoryAddress mem;
  mem.valid = true;
  mem.base = rm;
  mem.index = 0xFFu;
  if (rm == 4) {
    const auto sib = cur.next();
    mem.scale = static_cast<std::uint8_t>(1u << ((sib >> 6) & 0x3u));
    mem.index = (sib >> 3) & 0x7u;
    mem.base = sib & 0x7u;
    if (mem.index == 4) mem.index = 0xFFu;
    if (mod == 0 && mem.base == 5) {
      mem.base = 0xFFu;
      mem.displacement = static_cast<std::int32_t>(read_imm(cur.bytes, cur.off, 4));
    }
  } else if (mod == 0 && rm == 5) {
    mem.base = 0xFFu;
    mem.displacement = static_cast<std::int32_t>(read_imm(cur.bytes, cur.off, 4));
  }
  if (mod == 1) mem.displacement += static_cast<std::int8_t>(read_imm(cur.bytes, cur.off, 1));
  if (mod == 2) mem.displacement += static_cast<std::int32_t>(read_imm(cur.bytes, cur.off, 4));
  return mem;
}

const char* group2_name(std::uint8_t sub) {
  switch (sub & 7u) {
    case 0: return "rol";
    case 1: return "ror";
    case 2: return "rcl";
    case 3: return "rcr";
    case 4: return "shl";
    case 5: return "shr";
    case 6: return "sal";
    default: return "sar";
  }
}

void set_pc_relative(InstructionIR& ir,
                     std::uint64_t source_pc,
                     std::int64_t displacement,
                     common::PcRelativeTarget::Kind kind) {
  ir.pc_relative_target = common::PcRelativeTarget{
      source_pc,
      displacement,
      static_cast<std::uint64_t>(static_cast<std::int64_t>(source_pc) + displacement),
      kind,
  };
}

std::string mem_text(const MemoryAddress& mem) {
  std::ostringstream oss;
  oss << '[';
  bool wrote = false;
  if (mem.base != 0xFFu) {
    oss << reg_name(mem.base, 4);
    wrote = true;
  }
  if (mem.index != 0xFFu) {
    if (wrote) oss << '+';
    oss << reg_name(mem.index, 4);
    if (mem.scale != 1u) oss << '*' << static_cast<unsigned>(mem.scale);
    wrote = true;
  }
  if (mem.displacement != 0 || !wrote) {
    if (wrote && mem.displacement >= 0) oss << '+';
    oss << mem.displacement;
  }
  oss << ']';
  return oss.str();
}

InstructionIR decode_impl(const std::vector<std::uint8_t>& bytes, std::uint64_t address) {
  Cursor cur{bytes};
  InstructionIR ir;
  ir.address = address;
  bool op16 = false;
  while (!cur.eof()) {
    if (cur.peek() == 0x66) { op16 = true; ir.flags |= 1u; cur.next(); continue; }
    if (cur.peek() == 0x67 || cur.peek() == 0xF0 || cur.peek() == 0xF2 || cur.peek() == 0xF3 ||
        cur.peek() == 0x2E || cur.peek() == 0x36 || cur.peek() == 0x3E || cur.peek() == 0x26 ||
        cur.peek() == 0x64 || cur.peek() == 0x65) { cur.next(); continue; }
    break;
  }
  const auto opcode = cur.next();
  const auto width = static_cast<std::uint8_t>(op16 ? 2u : 4u);
  auto finish = [&]() {
    ir.size = cur.off;
    ir.encoding.assign(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cur.off));
    ir.can_reencode = true;
    return ir;
  };
  auto rel = [&](std::size_t w, common::PcRelativeTarget::Kind kind) {
    auto imm = read_imm(bytes, cur.off, w);
    auto s = w == 1 ? static_cast<std::int8_t>(imm) : static_cast<std::int32_t>(imm);
    ir.relative_target = address + cur.off + s;
    ir.has_relative_target = true;
    set_pc_relative(ir, address + cur.off, s, kind);
    push_operand(ir, std::to_string(ir.relative_target), OperandKind::imm, 4, ir.relative_target);
  };
  auto modrm_rr = [&](const char* mnemonic, bool reg_dst, bool simd = false) {
    ir.mnemonic = mnemonic;
    const auto modrm = cur.next();
    const auto mod = (modrm >> 6) & 0x3u;
    const auto reg = (modrm >> 3) & 0x7u;
    const auto rm = modrm & 0x7u;
    if (mod == 3) {
      const auto lhs = simd ? kXmm.at(reg) : reg_name(reg, width);
      const auto rhs = simd ? kXmm.at(rm) : reg_name(rm, width);
      if (reg_dst) {
        push_operand(ir, lhs, simd ? OperandKind::simd_reg : OperandKind::reg, simd ? 16 : width);
        push_operand(ir, rhs, simd ? OperandKind::simd_reg : OperandKind::reg, simd ? 16 : width);
      } else {
        push_operand(ir, rhs, simd ? OperandKind::simd_reg : OperandKind::reg, simd ? 16 : width);
        push_operand(ir, lhs, simd ? OperandKind::simd_reg : OperandKind::reg, simd ? 16 : width);
      }
    } else {
      const auto mem = decode_memory(cur, mod, rm);
      ir.memory = mem;
      const auto lhs = simd ? kXmm.at(reg) : reg_name(reg, width);
      if (reg_dst) {
        push_operand(ir, lhs, simd ? OperandKind::simd_reg : OperandKind::reg, simd ? 16 : width);
        push_operand(ir, mem_text(mem), OperandKind::mem, simd ? 16 : width);
      } else {
        push_operand(ir, mem_text(mem), OperandKind::mem, simd ? 16 : width);
        push_operand(ir, lhs, simd ? OperandKind::simd_reg : OperandKind::reg, simd ? 16 : width);
      }
    }
  };

  switch (opcode) {
    case 0x90: ir.mnemonic = "nop"; break;
    case 0x88: modrm_rr("mov", false); break;
    case 0x89: modrm_rr("mov", false); break;
    case 0x8A: modrm_rr("mov", true); break;
    case 0x8B: modrm_rr("mov", true); break;
    case 0x8D: modrm_rr("lea", true); break;
    case 0x01: modrm_rr("add", false); break;
    case 0x03: modrm_rr("add", true); break;
    case 0x29: modrm_rr("sub", false); break;
    case 0x2B: modrm_rr("sub", true); break;
    case 0x21: modrm_rr("and", false); break;
    case 0x23: modrm_rr("and", true); break;
    case 0x09: modrm_rr("or", false); break;
    case 0x0B: modrm_rr("or", true); break;
    case 0x31: modrm_rr("xor", false); break;
    case 0x33: modrm_rr("xor", true); break;
    case 0x39: modrm_rr("cmp", false); break;
    case 0x3B: modrm_rr("cmp", true); break;
    case 0x85: modrm_rr("test", false); break;
    case 0xE8: ir.mnemonic = "call"; rel(4, common::PcRelativeTarget::Kind::call); break;
    case 0xE9: ir.mnemonic = "jmp"; rel(4, common::PcRelativeTarget::Kind::branch); break;
    case 0xEB: ir.mnemonic = "jmp"; rel(1, common::PcRelativeTarget::Kind::branch); break;
    case 0xE0: ir.mnemonic = "loopne"; rel(1, common::PcRelativeTarget::Kind::branch); break;
    case 0xE1: ir.mnemonic = "loope"; rel(1, common::PcRelativeTarget::Kind::branch); break;
    case 0xE2: ir.mnemonic = "loop"; rel(1, common::PcRelativeTarget::Kind::branch); break;
    case 0xE3: ir.mnemonic = "jecxz"; rel(1, common::PcRelativeTarget::Kind::branch); break;
    case 0xC3: ir.mnemonic = "ret"; break;
    case 0x68: ir.mnemonic = "push"; push_operand(ir, std::to_string(read_imm(bytes, cur.off, 4)), OperandKind::imm, 4); break;
    case 0x6A: ir.mnemonic = "push"; push_operand(ir, std::to_string(read_imm(bytes, cur.off, 1)), OperandKind::imm, 1); break;
    case 0x80:
    case 0x81:
    case 0x83: {
      const auto modrm = cur.next();
      const auto mod = (modrm >> 6) & 0x3u;
      const auto sub = (modrm >> 3) & 7u;
      const auto rm = modrm & 7u;
      static const char* kGroup1[8] = {"add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"};
      ir.mnemonic = kGroup1[sub];
      if (mod == 3) {
        push_operand(ir, reg_name(rm, opcode == 0x80 ? 1 : width), OperandKind::reg, opcode == 0x80 ? 1 : width);
      } else {
        const auto mem = decode_memory(cur, mod, rm);
        ir.memory = mem;
        push_operand(ir, mem_text(mem), OperandKind::mem, opcode == 0x80 ? 1 : width);
      }
      auto imm = read_imm(bytes, cur.off, opcode == 0x81 ? width : 1);
      push_operand(ir, std::to_string(imm), OperandKind::imm, opcode == 0x81 ? width : 1, imm);
      break;
    }
    case 0xC6:
    case 0xC7: {
      const auto modrm = cur.next();
      const auto mod = (modrm >> 6) & 0x3u;
      const auto sub = (modrm >> 3) & 7u;
      const auto rm = modrm & 7u;
      ir.mnemonic = sub == 0 ? "mov" : "group11";
      if (mod == 3) {
        push_operand(ir, reg_name(rm, opcode == 0xC6 ? 1 : width), OperandKind::reg, opcode == 0xC6 ? 1 : width);
      } else {
        const auto mem = decode_memory(cur, mod, rm);
        ir.memory = mem;
        push_operand(ir, mem_text(mem), OperandKind::mem, opcode == 0xC6 ? 1 : width);
      }
      auto imm = read_imm(bytes, cur.off, opcode == 0xC6 ? 1 : width);
      push_operand(ir, std::to_string(imm), OperandKind::imm, opcode == 0xC6 ? 1 : width, imm);
      if (sub != 0) {
        ir.can_reencode = false;
        ir.skip_reason = "SKIP_REASON=x86 unsupported C6/C7 subop retained as original bytes.";
      }
      break;
    }
    case 0x9C: ir.mnemonic = "pushfd"; break;
    case 0x9D: ir.mnemonic = "popfd"; break;
    case 0xC9: ir.mnemonic = "leave"; break;
    case 0xF4: ir.mnemonic = "hlt"; break;
    case 0xC0:
    case 0xC1:
    case 0xD0:
    case 0xD1:
    case 0xD2:
    case 0xD3: {
      const auto modrm = cur.next();
      const auto mod = (modrm >> 6) & 3u;
      const auto sub = (modrm >> 3) & 7u;
      const auto rm = modrm & 7u;
      const auto op_width = static_cast<std::uint8_t>((opcode == 0xC0 || opcode == 0xD0 || opcode == 0xD2) ? 1u : width);
      ir.mnemonic = group2_name(sub);
      if (mod == 3) {
        push_operand(ir, reg_name(rm, op_width), OperandKind::reg, op_width);
      } else {
        const auto mem = decode_memory(cur, mod, rm);
        ir.memory = mem;
        push_operand(ir, mem_text(mem), OperandKind::mem, op_width);
      }
      if (opcode == 0xC0 || opcode == 0xC1) {
        auto imm = read_imm(bytes, cur.off, 1);
        push_operand(ir, std::to_string(imm), OperandKind::imm, 1, imm);
      } else if (opcode == 0xD2 || opcode == 0xD3) {
        push_operand(ir, "cl", OperandKind::reg, 1);
      } else {
        push_operand(ir, "1", OperandKind::imm, 1, 1);
      }
      break;
    }
    case 0xFF: {
      const auto modrm = cur.next();
      const auto mod = (modrm >> 6) & 3u;
      const auto sub = (modrm >> 3) & 7u;
      const auto rm = modrm & 7u;
      static const char* kGroup5[8] = {"inc", "dec", "call", "callf", "jmp", "jmpf", "push", "group5"};
      ir.mnemonic = kGroup5[sub];
      if (mod == 3) {
        push_operand(ir, reg_name(rm, width), OperandKind::reg, width);
      } else {
        const auto mem = decode_memory(cur, mod, rm);
        ir.memory = mem;
        push_operand(ir, mem_text(mem), OperandKind::mem, width);
        if ((sub == 2u || sub == 4u) && mem.base == 0xFFu) {
          ir.pc_relative_target = common::PcRelativeTarget{
              0,
              mem.displacement,
              static_cast<std::uint64_t>(static_cast<std::int64_t>(mem.displacement)),
              common::PcRelativeTarget::Kind::indirect_jump_via_table,
          };
        }
      }
      if (sub == 7) {
        ir.can_reencode = false;
        ir.skip_reason = "SKIP_REASON=x86 unsupported FF /7 retained as original bytes.";
      }
      break;
    }
    case 0x69:
    case 0x6B: modrm_rr("imul", true); read_imm(bytes, cur.off, opcode == 0x69 ? 4 : 1); break;
    default:
      if (opcode == 0x04 || opcode == 0x05 || opcode == 0x0C || opcode == 0x0D || opcode == 0x14 || opcode == 0x15 ||
          opcode == 0x1C || opcode == 0x1D || opcode == 0x24 || opcode == 0x25 || opcode == 0x2C || opcode == 0x2D ||
          opcode == 0x34 || opcode == 0x35 || opcode == 0x3C || opcode == 0x3D) {
        static const char* kAccNames[8] = {"add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"};
        const auto group = static_cast<unsigned>((opcode - 0x04u) / 8u);
        const auto acc_width = (opcode & 1u) ? width : 1u;
        ir.mnemonic = kAccNames[group];
        push_operand(ir, reg_name(0, acc_width), OperandKind::reg, acc_width);
        auto imm = read_imm(bytes, cur.off, acc_width == 1 ? 1 : width);
        push_operand(ir, std::to_string(imm), OperandKind::imm, acc_width == 1 ? 1 : width, imm);
      }
      else if (opcode >= 0x50 && opcode <= 0x57) { ir.mnemonic = "push"; push_operand(ir, reg_name(opcode - 0x50u, width), OperandKind::reg, width); }
      else if (opcode >= 0x58 && opcode <= 0x5F) { ir.mnemonic = "pop"; push_operand(ir, reg_name(opcode - 0x58u, width), OperandKind::reg, width); }
      else if (opcode >= 0x70 && opcode <= 0x7F) { ir.mnemonic = "jcc"; ir.condition = static_cast<ConditionCode>(opcode & 0xFu); rel(1, common::PcRelativeTarget::Kind::branch); }
      else if (opcode >= 0xB8 && opcode <= 0xBF) { ir.mnemonic = "mov"; push_operand(ir, reg_name(opcode - 0xB8u, width), OperandKind::reg, width); auto imm = read_imm(bytes, cur.off, width); push_operand(ir, std::to_string(imm), OperandKind::imm, width, imm); }
      else if (opcode == 0x0F) {
        const auto ext = cur.next();
        if (ext >= 0x80 && ext <= 0x8F) { ir.mnemonic = "jcc"; ir.condition = static_cast<ConditionCode>(ext & 0xFu); rel(4, common::PcRelativeTarget::Kind::branch); }
        else if (ext == 0xAF) modrm_rr("imul", true);
        else if (ext >= 0x90 && ext <= 0x9F) {
          ir.mnemonic = "setcc";
          ir.condition = static_cast<ConditionCode>(ext & 0xFu);
          const auto modrm = cur.next();
          const auto mod = (modrm >> 6) & 0x3u;
          const auto rm = modrm & 7u;
          if (mod == 3) push_operand(ir, reg_name(rm, 1), OperandKind::reg, 1);
          else {
            const auto mem = decode_memory(cur, mod, rm);
            ir.memory = mem;
            push_operand(ir, mem_text(mem), OperandKind::mem, 1);
          }
        }
        else if (ext >= 0x40 && ext <= 0x4F) {
          ir.mnemonic = "cmov";
          ir.condition = static_cast<ConditionCode>(ext & 0xFu);
          modrm_rr("cmov", true);
        }
        else if (ext == 0xB6 || ext == 0xB7) modrm_rr("movzx", true);
        else if (ext == 0xBE || ext == 0xBF) modrm_rr("movsx", true);
        else if (ext == 0xBC) modrm_rr("bsf", true);
        else if (ext == 0xBD) modrm_rr("bsr", true);
        else if (ext == 0x28 || ext == 0x29) modrm_rr(op16 ? "movapd" : "movaps", ext == 0x28, true);
        else if (ext == 0x58 || ext == 0x59 || ext == 0x5C || ext == 0x5E) modrm_rr(ext == 0x58 ? (op16 ? "addpd" : "addps") : ext == 0x59 ? (op16 ? "mulpd" : "mulps") : ext == 0x5C ? (op16 ? "subpd" : "subps") : (op16 ? "divpd" : "divps"), true, true);
        else if (ext == 0x6F || ext == 0x7F) modrm_rr(op16 ? "movdqa" : "movdqu", ext == 0x6F, true);
        else if (ext == 0x70) { modrm_rr("pshufd", true, true); auto imm=read_imm(bytes, cur.off, 1); push_operand(ir, std::to_string(imm), OperandKind::imm, 1, imm); }
        else if (ext == 0x74 || ext == 0x75 || ext == 0x76) modrm_rr(ext == 0x74 ? "pcmpeqb" : ext == 0x75 ? "pcmpeqw" : "pcmpeqd", true, true);
        else if (ext == 0x38) { const auto ext2 = cur.next(); if (ext2 == 0x00) modrm_rr("pshufb", true, true); else { ir.mnemonic = "opaque"; ir.can_reencode = false; ir.skip_reason = "SKIP_REASON=x86 0F38 decode-only."; } }
        else if (ext == 0x1E) { const auto modrm = cur.next(); if (modrm == 0xFB) { ir.mnemonic = "endbr32"; } else if (modrm == 0xFA) { ir.mnemonic = "endbr64"; } else { ir.mnemonic = "nop"; const auto mod = (modrm >> 6) & 3u; const auto rm = modrm & 7u; if (mod != 3) { const auto mem = decode_memory(cur, mod, rm); ir.memory = mem; push_operand(ir, mem_text(mem), OperandKind::mem, 4); } } }
        else if (ext == 0x1F) { ir.mnemonic = "nop"; const auto modrm = cur.next(); const auto mod = (modrm >> 6) & 3u; const auto rm = modrm & 7u; if (mod != 3) { const auto mem = decode_memory(cur, mod, rm); ir.memory = mem; push_operand(ir, mem_text(mem), OperandKind::mem, 4); } }
        else { ir.mnemonic = "opaque"; ir.can_reencode = false; ir.skip_reason = "SKIP_REASON=x86 unsupported two-byte opcode retained as opaque bytes."; }
      } else {
        ir.mnemonic = "opaque";
        ir.can_reencode = false;
        ir.skip_reason = "SKIP_REASON=x86 unsupported opcode retained as opaque bytes.";
      }
      break;
  }
  ir.size = cur.off;
  ir.encoding.assign(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cur.off));
  if (ir.mnemonic.empty()) ir.mnemonic = "opaque";
  if (ir.can_reencode == false && ir.skip_reason.empty()) ir.skip_reason = "SKIP_REASON=x86 decode-only instruction.";
  else if (!ir.encoding.empty()) ir.can_reencode = true;
  return ir;
}
}  // namespace

InstructionIR decode_instruction(const std::vector<std::uint8_t>& bytes, std::uint64_t address) {
  if (bytes.empty()) throw std::runtime_error("x86 decode: empty input");
  return decode_impl(bytes, address);
}

std::vector<InstructionIR> decode_stream(const std::vector<std::uint8_t>& bytes,
                                         std::uint64_t base_address,
                                         std::vector<common::Diagnostic>* diagnostics) {
  std::vector<InstructionIR> out;
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    std::vector<std::uint8_t> slice(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
    auto ir = decode_instruction(slice, base_address + offset);
    ir.offset = offset;
    if ((ir.mnemonic == "opaque") && diagnostics != nullptr) diagnostics->push_back({common::DiagnosticKind::unsupported_opcode, offset, ir.skip_reason});
    out.push_back(ir);
    offset += ir.size == 0 ? 1 : ir.size;
  }
  return out;
}

std::vector<std::uint8_t> reencode_instruction(const InstructionIR& instruction) {
  if (instruction.encoding.empty()) throw std::runtime_error("x86 reencode: instruction has no encoding");
  return instruction.encoding;
}

}  // namespace vmp::arch::x86
