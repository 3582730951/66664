#include <vmp/arch/x64/ir.h>

#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace vmp::arch::x64 {
namespace common = vmp::arch::common;
namespace {

struct PrefixState {
  std::uint32_t flags = prefix_none;
  std::uint8_t rex = 0;
  std::uint8_t vex_map = 0;
  std::uint8_t vex_pp = 0;
  std::uint8_t vex_l = 0;
  std::uint8_t vex_w = 0;
  bool has_vex = false;
  bool has_evex = false;
};

struct Cursor {
  const std::vector<std::uint8_t>& bytes;
  std::size_t off = 0;

  bool eof() const noexcept { return off >= bytes.size(); }
  std::uint8_t peek() const { return bytes.at(off); }
  std::uint8_t next() { return bytes.at(off++); }
  std::uint8_t at(std::size_t index) const { return bytes.at(index); }
};

constexpr std::array<const char*, 16> kGpr64{{
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
    "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
}};

constexpr std::array<const char*, 16> kXmm{{
    "xmm0",  "xmm1",  "xmm2",  "xmm3",  "xmm4",  "xmm5",  "xmm6",  "xmm7",
    "xmm8",  "xmm9",  "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15",
}};

constexpr std::array<const char*, 16> kYmm{{
    "ymm0",  "ymm1",  "ymm2",  "ymm3",  "ymm4",  "ymm5",  "ymm6",  "ymm7",
    "ymm8",  "ymm9",  "ymm10", "ymm11", "ymm12", "ymm13", "ymm14", "ymm15",
}};

struct DecodedOperand {
  OperandKind kind = OperandKind::none;
  std::string text;
  std::uint8_t size = 0;
  std::uint64_t imm = 0;
  MemoryAddress memory{};
};

std::string hex_u64(std::uint64_t value) {
  std::ostringstream oss;
  oss << "0x" << std::hex << value;
  return oss.str();
}

std::string gpr_name(std::uint8_t index, std::uint8_t width) {
  static constexpr std::array<const char*, 16> kGpr32{{
      "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi",
      "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d",
  }};
  static constexpr std::array<const char*, 16> kGpr16{{
      "ax", "cx", "dx", "bx", "sp", "bp", "si", "di",
      "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w",
  }};
  static constexpr std::array<const char*, 16> kGpr8{{
      "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil",
      "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b",
  }};
  switch (width) {
    case 1: return kGpr8.at(index);
    case 2: return kGpr16.at(index);
    case 4: return kGpr32.at(index);
    default: return kGpr64.at(index);
  }
}

std::string simd_name(std::uint8_t index, std::uint8_t width) {
  return width == 32 ? kYmm.at(index) : kXmm.at(index);
}

std::uint64_t read_imm(const std::vector<std::uint8_t>& bytes, std::size_t& off, std::size_t width) {
  if (off + width > bytes.size()) {
    throw std::runtime_error("x64 decode: truncated immediate");
  }
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < width; ++i) {
    value |= static_cast<std::uint64_t>(bytes[off + i]) << (i * 8u);
  }
  off += width;
  return value;
}

std::int32_t sign_extend32(std::uint64_t value, std::size_t width) {
  switch (width) {
    case 1: return static_cast<std::int8_t>(value);
    case 2: return static_cast<std::int16_t>(value);
    default: return static_cast<std::int32_t>(value);
  }
}

ConditionCode decode_cc(std::uint8_t code) { return static_cast<ConditionCode>(code & 0xFu); }

bool memory_destination_opcode(std::string_view mnemonic) {
  return mnemonic == "mov" || mnemonic == "movaps" || mnemonic == "movapd" || mnemonic == "movdqa" ||
         mnemonic == "movdqu" || mnemonic == "vmovdqa" || mnemonic == "vmovdqu" || mnemonic == "vmovdqu64";
}

void assign_direct_pc_relative(InstructionIR& ir, std::uint64_t source_pc) {
  if (!ir.has_relative_target) {
    return;
  }
  const auto displacement = static_cast<std::int64_t>(ir.relative_target) - static_cast<std::int64_t>(source_pc);
  const auto kind = ir.mnemonic == "call" ? common::PcRelativeTarget::Kind::call
                                          : common::PcRelativeTarget::Kind::branch;
  ir.pc_relative_target = common::PcRelativeTarget{source_pc, displacement, ir.relative_target, kind};
}

void assign_memory_pc_relative(InstructionIR& ir, std::uint64_t source_pc) {
  if (!ir.memory.valid || !ir.memory.rip_relative) {
    return;
  }
  auto kind = common::PcRelativeTarget::Kind::load;
  if (ir.mnemonic == "lea") {
    kind = common::PcRelativeTarget::Kind::address_materialize;
  } else if (ir.mnemonic == "jmp" || ir.mnemonic == "call") {
    kind = common::PcRelativeTarget::Kind::indirect_jump_via_table;
  } else if (!ir.operand_kinds.empty() && ir.operand_kinds.front() == OperandKind::mem &&
             memory_destination_opcode(ir.mnemonic)) {
    kind = common::PcRelativeTarget::Kind::store;
  }
  ir.pc_relative_target = common::make_pc_relative_target(source_pc, ir.memory.displacement, kind);
}

void append_operand(InstructionIR& ir, const DecodedOperand& op) {
  ir.operands.push_back(op.text);
  ir.operand_kinds.push_back(op.kind);
  ir.operand_sizes.push_back(op.size);
  if (op.kind == OperandKind::imm) {
    ir.immediate_values.push_back(op.imm);
  }
  if (op.kind == OperandKind::mem) {
    ir.memory = op.memory;
  }
}

PrefixState parse_prefixes(Cursor& cur) {
  PrefixState p;
  bool keep = true;
  while (keep && !cur.eof()) {
    switch (cur.peek()) {
      case 0x66: p.flags |= prefix_66; cur.next(); break;
      case 0x67: p.flags |= prefix_67; cur.next(); break;
      case 0xF0: p.flags |= prefix_lock; cur.next(); break;
      case 0xF2: p.flags |= prefix_repne; cur.next(); break;
      case 0xF3: p.flags |= prefix_rep; cur.next(); break;
      case 0x2E: p.flags |= prefix_seg_cs; cur.next(); break;
      case 0x36: p.flags |= prefix_seg_ss; cur.next(); break;
      case 0x3E: p.flags |= prefix_seg_ds; cur.next(); break;
      case 0x26: p.flags |= prefix_seg_es; cur.next(); break;
      case 0x64: p.flags |= prefix_seg_fs; cur.next(); break;
      case 0x65: p.flags |= prefix_seg_gs; cur.next(); break;
      default:
        keep = false;
        break;
    }
  }
  if (!cur.eof() && cur.peek() >= 0x40 && cur.peek() <= 0x4F) {
    p.rex = cur.next();
    p.flags |= prefix_rex;
    if ((p.rex & 0x8u) != 0) p.flags |= prefix_rex_w;
    if ((p.rex & 0x4u) != 0) p.flags |= prefix_rex_r;
    if ((p.rex & 0x2u) != 0) p.flags |= prefix_rex_x;
    if ((p.rex & 0x1u) != 0) p.flags |= prefix_rex_b;
  }
  if (!cur.eof() && cur.peek() == 0xC5) {
    p.flags |= prefix_vex;
    p.has_vex = true;
    cur.next();
    const auto b1 = cur.next();
    p.vex_map = 1;
    p.vex_pp = b1 & 0x3u;
    p.vex_l = (b1 >> 2) & 1u;
    p.vex_w = 0;
  } else if (!cur.eof() && cur.peek() == 0xC4) {
    p.flags |= prefix_vex;
    p.has_vex = true;
    cur.next();
    const auto b1 = cur.next();
    const auto b2 = cur.next();
    p.vex_map = b1 & 0x1Fu;
    p.vex_pp = b2 & 0x3u;
    p.vex_l = (b2 >> 2) & 1u;
    p.vex_w = (b2 >> 7) & 1u;
  } else if (!cur.eof() && cur.peek() == 0x62 && cur.bytes.size() - cur.off >= 4) {
    p.flags |= prefix_evex;
    p.has_evex = true;
    cur.next();
    const auto b1 = cur.next();
    const auto b2 = cur.next();
    const auto b3 = cur.next();
    p.vex_map = b1 & 0x3u;
    p.vex_pp = b2 & 0x3u;
    p.vex_l = (b3 >> 5) & 0x3u;
    p.vex_w = (b2 >> 7) & 1u;
  }
  return p;
}

std::uint8_t rex_bit(std::uint8_t rex, std::uint8_t mask, std::uint8_t shift) {
  return ((rex & mask) != 0u) ? (1u << shift) : 0u;
}

DecodedOperand decode_reg_operand(std::uint8_t reg, std::uint8_t width, OperandKind kind = OperandKind::reg) {
  DecodedOperand op;
  op.kind = kind;
  op.size = width;
  op.text = kind == OperandKind::simd_reg ? simd_name(reg, width) : gpr_name(reg, width);
  return op;
}

DecodedOperand decode_imm_operand(std::uint64_t imm, std::uint8_t width) {
  DecodedOperand op;
  op.kind = OperandKind::imm;
  op.size = width;
  op.imm = imm;
  op.text = hex_u64(imm);
  return op;
}

DecodedOperand decode_modrm_operand(Cursor& cur,
                                    const PrefixState& prefix,
                                    std::uint8_t width,
                                    bool simd,
                                    std::uint8_t* out_reg,
                                    std::uint8_t* out_rm) {
  const auto modrm = cur.next();
  const std::uint8_t mod = (modrm >> 6) & 0x3u;
  const std::uint8_t reg = ((modrm >> 3) & 0x7u) | (((prefix.flags & prefix_rex_r) != 0u) ? 8u : 0u);
  const std::uint8_t rm = (modrm & 0x7u) | (((prefix.flags & prefix_rex_b) != 0u) ? 8u : 0u);
  if (out_reg) *out_reg = reg;
  if (out_rm) *out_rm = rm;
  if (mod == 3) {
    return decode_reg_operand(rm, width, simd ? OperandKind::simd_reg : OperandKind::reg);
  }
  DecodedOperand op;
  op.kind = OperandKind::mem;
  op.size = width;
  op.memory.valid = true;
  op.memory.base = rm;
  op.memory.index = 0xFFu;
  op.memory.scale = 1;
  if ((rm & 7u) == 4u) {
    const auto sib = cur.next();
    const std::uint8_t scale = (sib >> 6) & 0x3u;
    const std::uint8_t index = ((sib >> 3) & 0x7u) | (((prefix.flags & prefix_rex_x) != 0u) ? 8u : 0u);
    const std::uint8_t base = (sib & 0x7u) | (((prefix.flags & prefix_rex_b) != 0u) ? 8u : 0u);
    op.memory.scale = static_cast<std::uint8_t>(1u << scale);
    if ((sib & 0x7u) == 5u && mod == 0) {
      op.memory.base = 0xFFu;
      op.memory.rip_relative = true;
      op.memory.displacement = sign_extend32(read_imm(cur.bytes, cur.off, 4), 4);
    } else {
      op.memory.base = base;
    }
    if (((sib >> 3) & 0x7u) != 4u) {
      op.memory.index = index;
    }
  } else if ((rm & 7u) == 5u && mod == 0) {
    op.memory.base = 0xFFu;
    op.memory.rip_relative = true;
    op.memory.displacement = sign_extend32(read_imm(cur.bytes, cur.off, 4), 4);
  }
  if (mod == 1) {
    op.memory.displacement += sign_extend32(read_imm(cur.bytes, cur.off, 1), 1);
  } else if (mod == 2) {
    op.memory.displacement += sign_extend32(read_imm(cur.bytes, cur.off, 4), 4);
  }
  std::ostringstream oss;
  oss << '[';
  bool wrote = false;
  if (op.memory.rip_relative) {
    oss << "rip";
    wrote = true;
  } else if (op.memory.base != 0xFFu) {
    oss << gpr_name(op.memory.base, 8);
    wrote = true;
  }
  if (op.memory.index != 0xFFu) {
    if (wrote) oss << '+';
    oss << gpr_name(op.memory.index, 8);
    if (op.memory.scale != 1u) oss << '*' << static_cast<unsigned>(op.memory.scale);
    wrote = true;
  }
  if (op.memory.displacement != 0 || !wrote) {
    if (wrote && op.memory.displacement >= 0) oss << '+';
    oss << op.memory.displacement;
  }
  oss << ']';
  op.text = oss.str();
  return op;
}

InstructionIR make_instruction(Cursor& cur, std::size_t start, std::uint64_t address) {
  InstructionIR ir;
  ir.address = address;
  ir.offset = 0;
  ir.size = cur.off - start;
  ir.can_reencode = true;
  ir.encoding.assign(cur.bytes.begin() + static_cast<std::ptrdiff_t>(start),
                     cur.bytes.begin() + static_cast<std::ptrdiff_t>(cur.off));
  return ir;
}

InstructionIR decode_with_prefix(const std::vector<std::uint8_t>& bytes, std::uint64_t address) {
  Cursor cur{bytes};
  const auto start = cur.off;
  const auto prefix = parse_prefixes(cur);
  if (cur.eof()) throw std::runtime_error("x64 decode: missing opcode");
  InstructionIR ir;
  ir.address = address;
  ir.flags = prefix.flags;
  const auto opcode = cur.next();
  const std::uint8_t gpr_width = (prefix.flags & prefix_rex_w) != 0u ? 8u : ((prefix.flags & prefix_66) != 0u ? 2u : 4u);

  auto finish = [&]() {
    ir.size = cur.off - start;
    ir.can_reencode = true;
    ir.encoding.assign(bytes.begin() + static_cast<std::ptrdiff_t>(start), bytes.begin() + static_cast<std::ptrdiff_t>(cur.off));
    const auto source_pc = address + static_cast<std::uint64_t>(cur.off);
    assign_direct_pc_relative(ir, source_pc);
    if (!ir.pc_relative_target.has_value()) {
      assign_memory_pc_relative(ir, source_pc);
    }
    return ir;
  };

  auto decode_rel_target = [&](std::size_t width) {
    const auto rel = sign_extend32(read_imm(bytes, cur.off, width), width);
    ir.relative_target = address + static_cast<std::uint64_t>(cur.off + rel);
    ir.has_relative_target = true;
    append_operand(ir, decode_imm_operand(ir.relative_target, 8));
  };

  auto decode_reg_mem = [&](const char* mnemonic, bool reg_dst, std::uint8_t width, bool simd=false) {
    ir.mnemonic = mnemonic;
    std::uint8_t reg = 0, rm = 0;
    const auto rmop = decode_modrm_operand(cur, prefix, width, simd, &reg, &rm);
    const auto regop = decode_reg_operand(reg, width, simd ? OperandKind::simd_reg : OperandKind::reg);
    if (reg_dst) {
      append_operand(ir, regop);
      append_operand(ir, rmop);
    } else {
      append_operand(ir, rmop);
      append_operand(ir, regop);
    }
  };

  if (prefix.has_vex || prefix.has_evex) {
    std::uint8_t reg = 0, rm = 0;
    const std::uint8_t vec_width = prefix.has_evex ? 16 : (prefix.vex_l ? 32u : 16u);
    const auto map = prefix.vex_map == 0 ? 1u : prefix.vex_map;
    if (opcode == 0x77 && prefix.vex_map == 1 && prefix.vex_pp == 0) {
      ir.mnemonic = prefix.vex_l ? "vzeroall" : "vzeroupper";
      return finish();
    }
    if (prefix.has_evex && (opcode == 0x6F || opcode == 0x7F || opcode == 0xEF)) {
      const auto rmop = decode_modrm_operand(cur, prefix, 16, true, &reg, &rm);
      append_operand(ir, decode_reg_operand(reg, 16, OperandKind::simd_reg));
      append_operand(ir, rmop);
      ir.mnemonic = opcode == 0xEF ? "vpxorq" : (opcode == 0x6F ? "vmovdqu64" : "vmovdqu64");
      ir.can_reencode = false;
      ir.skip_reason = "SKIP_REASON=EVEX foundation encodings keep original bytes for round-trip but are decode-only.";
      return finish();
    }
    if (map == 1 && (opcode == 0x58 || opcode == 0x59 || opcode == 0xEF || opcode == 0x6F || opcode == 0x7F)) {
      static const char* kAddNames[4] = {"vaddps", "vaddpd", "vaddss", "vaddsd"};
      static const char* kMulNames[4] = {"vmulps", "vmulpd", "vmulss", "vmulsd"};
      const auto rmop = decode_modrm_operand(cur, prefix, vec_width, true, &reg, &rm);
      const auto regop = decode_reg_operand(reg, vec_width, OperandKind::simd_reg);
      ir.mnemonic = opcode == 0x58 ? kAddNames[prefix.vex_pp & 0x3u]
                  : opcode == 0x59 ? kMulNames[prefix.vex_pp & 0x3u]
                  : opcode == 0xEF ? "vpxor"
                  : opcode == 0x6F ? ((prefix.vex_pp == 1) ? "vmovdqa" : "vmovdqu")
                                   : ((prefix.vex_pp == 1) ? "vmovdqa" : "vmovdqu");
      if (opcode == 0x7F) {
        append_operand(ir, rmop);
        append_operand(ir, regop);
      } else {
        append_operand(ir, regop);
        append_operand(ir, rmop);
      }
      return finish();
    }
    if (map == 2 && opcode == 0x00) {
      ir.mnemonic = "vpshufb";
      const auto rmop = decode_modrm_operand(cur, prefix, vec_width, true, &reg, &rm);
      append_operand(ir, decode_reg_operand(reg, vec_width, OperandKind::simd_reg));
      append_operand(ir, rmop);
      return finish();
    }
    if (map == 2 && opcode >= 0x98 && opcode <= 0x9F) {
      ir.mnemonic = (opcode <= 0x9B) ? "vfmadd" : "vfmsub";
      const auto rmop = decode_modrm_operand(cur, prefix, vec_width, true, &reg, &rm);
      append_operand(ir, decode_reg_operand(reg, vec_width, OperandKind::simd_reg));
      append_operand(ir, rmop);
      ir.can_reencode = false;
      ir.skip_reason = "SKIP_REASON=VEX/EVEX FMA re-encoding is decode-only in subtask 19.";
      return finish();
    }
    ir.mnemonic = prefix.has_evex ? "evex_opaque" : "vex_opaque";
    ir.can_reencode = false;
    ir.skip_reason = prefix.has_evex ? "SKIP_REASON=EVEX opaque decode keeps original bytes only." : "SKIP_REASON=VEX opaque decode keeps original bytes only.";
    return finish();
  }

  switch (opcode) {
    case 0x90:
      ir.mnemonic = "nop";
      break;
    case 0x68:
      ir.mnemonic = "push";
      append_operand(ir, decode_imm_operand(read_imm(bytes, cur.off, 4), 4));
      break;
    case 0x6A:
      ir.mnemonic = "push";
      append_operand(ir, decode_imm_operand(read_imm(bytes, cur.off, 1), 1));
      break;
    case 0xE8:
      ir.mnemonic = "call";
      decode_rel_target(4);
      break;
    case 0xE9:
      ir.mnemonic = "jmp";
      decode_rel_target(4);
      break;
    case 0xEB:
      ir.mnemonic = "jmp";
      decode_rel_target(1);
      break;
    case 0xC3:
      ir.mnemonic = "ret";
      break;
    case 0xC2:
      ir.mnemonic = "ret";
      append_operand(ir, decode_imm_operand(read_imm(bytes, cur.off, 2), 2));
      break;
    case 0x88: decode_reg_mem("mov", false, 1); break;
    case 0x89: decode_reg_mem("mov", false, gpr_width); break;
    case 0x8A: decode_reg_mem("mov", true, 1); break;
    case 0x8B: decode_reg_mem("mov", true, gpr_width); break;
    case 0x8D: decode_reg_mem("lea", true, gpr_width); break;
    case 0x01: decode_reg_mem("add", false, gpr_width); break;
    case 0x03: decode_reg_mem("add", true, gpr_width); break;
    case 0x29: decode_reg_mem("sub", false, gpr_width); break;
    case 0x2B: decode_reg_mem("sub", true, gpr_width); break;
    case 0x21: decode_reg_mem("and", false, gpr_width); break;
    case 0x23: decode_reg_mem("and", true, gpr_width); break;
    case 0x09: decode_reg_mem("or", false, gpr_width); break;
    case 0x0B: decode_reg_mem("or", true, gpr_width); break;
    case 0x31: decode_reg_mem("xor", false, gpr_width); break;
    case 0x33: decode_reg_mem("xor", true, gpr_width); break;
    case 0x39: decode_reg_mem("cmp", false, gpr_width); break;
    case 0x3B: decode_reg_mem("cmp", true, gpr_width); break;
    case 0x85: decode_reg_mem("test", false, gpr_width); break;
    case 0x87: decode_reg_mem("xchg", false, gpr_width); break;
    case 0x63: decode_reg_mem("movsxd", true, 8); break;
    case 0x80:
    case 0x81:
    case 0x83: {
      std::uint8_t reg = 0, rm = 0;
      const auto op_width = opcode == 0x80 ? 1u : gpr_width;
      const auto rmop = decode_modrm_operand(cur, prefix, op_width, false, &reg, &rm);
      const auto imm_width = opcode == 0x81 ? gpr_width : 1u;
      const auto imm = read_imm(bytes, cur.off, imm_width);
      static const char* kGroup1[8] = {"add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"};
      ir.mnemonic = kGroup1[reg & 0x7u];
      append_operand(ir, rmop);
      append_operand(ir, decode_imm_operand(imm, imm_width));
      break;
    }
    case 0xC1:
    case 0xD1:
    case 0xD3: {
      std::uint8_t reg = 0, rm = 0;
      const auto rmop = decode_modrm_operand(cur, prefix, gpr_width, false, &reg, &rm);
      static const char* kShiftNames[8] = {"rol", "ror", "rcl", "rcr", "shl", "shr", "sal", "sar"};
      ir.mnemonic = kShiftNames[reg & 0x7u];
      append_operand(ir, rmop);
      if (opcode == 0xC1) {
        append_operand(ir, decode_imm_operand(read_imm(bytes, cur.off, 1), 1));
      } else if (opcode == 0xD3) {
        append_operand(ir, decode_reg_operand(1, 1));
      } else {
        append_operand(ir, decode_imm_operand(1, 1));
      }
      break;
    }
    case 0xC6:
    case 0xC7: {
      std::uint8_t reg = 0, rm = 0;
      const auto width = opcode == 0xC6 ? 1u : ((prefix.flags & prefix_rex_w) != 0u ? 8u : gpr_width);
      const auto rmop = decode_modrm_operand(cur, prefix, width, false, &reg, &rm);
      if ((reg & 0x7u) != 0u) {
        ir.mnemonic = "group11";
        ir.can_reencode = false;
        ir.skip_reason = "SKIP_REASON=Unsupported C6/C7 subop retained as original bytes.";
      } else {
        ir.mnemonic = "mov";
        append_operand(ir, rmop);
        const auto imm_width = opcode == 0xC6 ? 1u : ((prefix.flags & prefix_rex_w) != 0u ? 4u : gpr_width);
        append_operand(ir, decode_imm_operand(read_imm(bytes, cur.off, imm_width), imm_width));
      }
      break;
    }
    case 0xF6:
    case 0xF7: {
      std::uint8_t reg = 0, rm = 0;
      const auto rmop = decode_modrm_operand(cur, prefix, gpr_width, false, &reg, &rm);
      switch (reg & 7u) {
        case 0: ir.mnemonic = "test"; append_operand(ir, rmop); append_operand(ir, decode_imm_operand(read_imm(bytes, cur.off, gpr_width), gpr_width)); break;
        case 2: ir.mnemonic = "not"; append_operand(ir, rmop); break;
        case 3: ir.mnemonic = "neg"; append_operand(ir, rmop); break;
        case 4: ir.mnemonic = "mul"; append_operand(ir, rmop); break;
        case 5: ir.mnemonic = "imul"; append_operand(ir, rmop); break;
        case 6: ir.mnemonic = "div"; append_operand(ir, rmop); break;
        case 7: ir.mnemonic = "idiv"; append_operand(ir, rmop); break;
        default: ir.mnemonic = "group3"; append_operand(ir, rmop); break;
      }
      break;
    }
    case 0xFF: {
      std::uint8_t reg = 0, rm = 0;
      const auto rmop = decode_modrm_operand(cur, prefix, gpr_width, false, &reg, &rm);
      switch (reg & 7u) {
        case 0: ir.mnemonic = "inc"; break;
        case 1: ir.mnemonic = "dec"; break;
        case 2: ir.mnemonic = "call"; break;
        case 4: ir.mnemonic = "jmp"; break;
        case 6: ir.mnemonic = "push"; break;
        default: ir.mnemonic = "ff_group"; break;
      }
      append_operand(ir, rmop);
      break;
    }
    case 0x04: case 0x05: case 0x0C: case 0x0D:
    case 0x14: case 0x15: case 0x1C: case 0x1D:
    case 0x24: case 0x25: case 0x2C: case 0x2D:
    case 0x34: case 0x35: case 0x3C: case 0x3D: {
      static const char* kAccNames[8] = {"add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"};
      const auto group = static_cast<unsigned>((opcode - 0x04u) / 8u);
      const auto width = (opcode & 1u) ? gpr_width : 1u;
      ir.mnemonic = kAccNames[group];
      append_operand(ir, decode_reg_operand(0, width));
      append_operand(ir, decode_imm_operand(read_imm(bytes, cur.off, width == 1 ? 1 : (width == 2 ? 2 : 4)), width == 1 ? 1 : (width == 2 ? 2 : 4)));
      break;
    }
    case 0x9C:
      ir.mnemonic = "pushf";
      break;
    case 0x9D:
      ir.mnemonic = "popf";
      break;
    case 0xC9:
      ir.mnemonic = "leave";
      break;
    case 0x0F: {
      if (cur.eof()) throw std::runtime_error("x64 decode: truncated 0F opcode");
      const auto ext = cur.next();
      if (ext >= 0x80 && ext <= 0x8F) {
        ir.mnemonic = "jcc";
        ir.condition = decode_cc(ext & 0xFu);
        decode_rel_target(4);
        break;
      }
      switch (ext) {
        case 0x05: ir.mnemonic = "syscall"; break;
        case 0x90: case 0x91: case 0x92: case 0x93:
        case 0x94: case 0x95: case 0x96: case 0x97:
        case 0x98: case 0x99: case 0x9A: case 0x9B:
        case 0x9C: case 0x9D: case 0x9E: case 0x9F: {
          std::uint8_t reg = 0, rm = 0;
          ir.mnemonic = "setcc";
          ir.condition = decode_cc(ext & 0xFu);
          append_operand(ir, decode_modrm_operand(cur, prefix, 1, false, &reg, &rm));
          break;
        }
        case 0x40: case 0x41: case 0x42: case 0x43:
        case 0x44: case 0x45: case 0x46: case 0x47:
        case 0x48: case 0x49: case 0x4A: case 0x4B:
        case 0x4C: case 0x4D: case 0x4E: case 0x4F:
          decode_reg_mem("cmov", true, gpr_width);
          ir.condition = decode_cc(ext & 0xFu);
          break;
        case 0x28:
        case 0x29:
          decode_reg_mem((prefix.flags & prefix_66) ? "movapd" : "movaps", ext == 0x28, 16, true);
          break;
        case 0x2F:
          decode_reg_mem((prefix.flags & prefix_66) ? "comisd" : "comiss", true, 16, true);
          break;
        case 0x54:
          decode_reg_mem((prefix.flags & prefix_66) ? "andpd" : "andps", true, 16, true);
          break;
        case 0x56:
          decode_reg_mem((prefix.flags & prefix_66) ? "orpd" : "orps", true, 16, true);
          break;
        case 0x57:
          decode_reg_mem((prefix.flags & prefix_66) ? "xorpd" : "xorps", true, 16, true);
          break;
        case 0x58:
        case 0x5C:
        case 0x59:
        case 0x5E: {
          static const char* kPsNames[4] = {"addps", "subps", "mulps", "divps"};
          static const char* kPdNames[4] = {"addpd", "subpd", "mulpd", "divpd"};
          static const char* kSsNames[4] = {"addss", "subss", "mulss", "divss"};
          static const char* kSdNames[4] = {"addsd", "subsd", "mulsd", "divsd"};
          const int idx = ext == 0x58 ? 0 : ext == 0x5C ? 1 : ext == 0x59 ? 2 : 3;
          const char* name = (prefix.flags & prefix_repne) ? kSdNames[idx]
                            : (prefix.flags & prefix_rep) ? kSsNames[idx]
                            : (prefix.flags & prefix_66) ? kPdNames[idx]
                                                         : kPsNames[idx];
          decode_reg_mem(name, true, 16, true);
          break;
        }
        case 0x6F:
        case 0x7F:
          decode_reg_mem((prefix.flags & prefix_66) ? "movdqa" : ((prefix.flags & prefix_rep) ? "movdqu" : "movq"), ext == 0x6F, 16, true);
          break;
        case 0x70:
          decode_reg_mem("pshufd", true, 16, true);
          append_operand(ir, decode_imm_operand(read_imm(bytes, cur.off, 1), 1));
          break;
        case 0x74:
        case 0x75:
        case 0x76:
          decode_reg_mem(ext == 0x74 ? "pcmpeqb" : ext == 0x75 ? "pcmpeqw" : "pcmpeqd", true, 16, true);
          break;
        case 0x77:
          ir.mnemonic = "emms";
          break;
        case 0xAF:
          decode_reg_mem("imul", true, gpr_width);
          break;
        case 0xA2:
          ir.mnemonic = "cpuid";
          break;
        case 0xA3:
        case 0xAB:
        case 0xB3:
        case 0xBB:
          decode_reg_mem(ext == 0xA3 ? "bt" : ext == 0xAB ? "bts" : ext == 0xB3 ? "btr" : "btc", false, gpr_width);
          break;
        case 0xB6:
        case 0xB7:
          decode_reg_mem(ext == 0xB6 ? "movzx" : "movzx", true, ext == 0xB6 ? 1 : 2);
          break;
        case 0xBE:
        case 0xBF:
          decode_reg_mem(ext == 0xBE ? "movsx" : "movsx", true, ext == 0xBE ? 1 : 2);
          break;
        case 0xBC:
          decode_reg_mem((prefix.flags & prefix_rep) ? "tzcnt" : "bsf", true, gpr_width);
          break;
        case 0xBD:
          decode_reg_mem((prefix.flags & prefix_rep) ? "lzcnt" : "bsr", true, gpr_width);
          break;
        case 0x1F: {
          ir.mnemonic = "nop";
          std::uint8_t reg = 0, rm = 0;
          auto operand = decode_modrm_operand(cur, prefix, gpr_width, false, &reg, &rm);
          if (operand.kind == OperandKind::mem) {
            ir.memory = operand.memory;
            append_operand(ir, operand);
          }
          break;
        }
        case 0x1E: {
          const auto modrm = cur.next();
          ir.mnemonic = (prefix.flags & prefix_rep) ? ((modrm == 0xFA) ? "endbr64" : "endbr32") : "nop";
          break;
        }
        case 0x31:
          ir.mnemonic = "rdtsc";
          break;
        case 0x01: {
          if (cur.eof()) throw std::runtime_error("x64 decode: truncated 0F01");
          const auto next = cur.peek();
          if (next == 0xF8) {
            cur.next();
            ir.mnemonic = "swapgs";
          } else if (next == 0xF9) {
            cur.next();
            ir.mnemonic = "rdtscp";
          } else if (next == 0xC8) {
            cur.next();
            ir.mnemonic = "monitor";
          } else if (next == 0xC9) {
            cur.next();
            ir.mnemonic = "mwait";
          } else {
            std::uint8_t reg = 0, rm = 0;
            auto rmop = decode_modrm_operand(cur, prefix, gpr_width, false, &reg, &rm);
            ir.mnemonic = "system";
            append_operand(ir, rmop);
          }
          break;
        }
        case 0x38: {
          const auto ext2 = cur.next();
          switch (ext2) {
            case 0x00: decode_reg_mem("pshufb", true, 16, true); break;
            case 0xF0: decode_reg_mem("movbe", true, gpr_width); break;
            case 0xF1: decode_reg_mem("movbe", false, gpr_width); break;
            default:
              ir.mnemonic = "0f38_opaque";
              ir.can_reencode = false;
              ir.skip_reason = "SKIP_REASON=0F38 sub-map is decode-only for unsupported encodings.";
              break;
          }
          break;
        }
        default:
          ir.mnemonic = "0f_opaque";
          ir.can_reencode = false;
          ir.skip_reason = "SKIP_REASON=Two-byte opcode is decode-only outside the exercised subset.";
          break;
      }
      break;
    }
    default:
      if (opcode >= 0x50 && opcode <= 0x57) {
        ir.mnemonic = "push";
        append_operand(ir, decode_reg_operand(static_cast<std::uint8_t>((opcode - 0x50u) | (((prefix.flags & prefix_rex_b) != 0u) ? 8u : 0u)), gpr_width));
      } else if (opcode >= 0x58 && opcode <= 0x5F) {
        ir.mnemonic = "pop";
        append_operand(ir, decode_reg_operand(static_cast<std::uint8_t>((opcode - 0x58u) | (((prefix.flags & prefix_rex_b) != 0u) ? 8u : 0u)), gpr_width));
      } else if (opcode >= 0x70 && opcode <= 0x7F) {
        ir.mnemonic = "jcc";
        ir.condition = decode_cc(opcode & 0xFu);
        decode_rel_target(1);
      } else if (opcode >= 0xB8 && opcode <= 0xBF) {
        ir.mnemonic = ((prefix.flags & prefix_rex_w) != 0u) ? "movabs" : "mov";
        const auto reg = static_cast<std::uint8_t>((opcode - 0xB8u) | (((prefix.flags & prefix_rex_b) != 0u) ? 8u : 0u));
        append_operand(ir, decode_reg_operand(reg, gpr_width));
        append_operand(ir, decode_imm_operand(read_imm(bytes, cur.off, (prefix.flags & prefix_rex_w) != 0u ? 8 : 4), (prefix.flags & prefix_rex_w) != 0u ? 8 : 4));
      } else {
        ir.mnemonic = "opaque";
        ir.can_reencode = false;
        ir.skip_reason = "SKIP_REASON=Opcode is outside the validated subset and is retained as opaque bytes.";
      }
      break;
  }
  return finish();
}

}  // namespace

InstructionIR decode_instruction(const std::vector<std::uint8_t>& bytes, std::uint64_t address) {
  if (bytes.empty()) {
    throw std::runtime_error("x64 decode: empty input");
  }
  auto ir = decode_with_prefix(bytes, address);
  ir.offset = 0;
  return ir;
}

std::vector<InstructionIR> decode_stream(const std::vector<std::uint8_t>& bytes,
                                         std::uint64_t base_address,
                                         std::vector<common::Diagnostic>* diagnostics) {
  std::vector<InstructionIR> out;
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    try {
      std::vector<std::uint8_t> slice(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
      auto ir = decode_instruction(slice, base_address + offset);
      ir.offset = offset;
      if (ir.size == 0) throw std::runtime_error("x64 decode: zero-sized instruction");
      if (ir.mnemonic == "opaque" || ir.mnemonic == "0f_opaque") {
        if (diagnostics != nullptr) diagnostics->push_back({common::DiagnosticKind::unsupported_opcode, offset, ir.skip_reason});
      }
      out.push_back(ir);
      offset += ir.size;
    } catch (const std::exception& ex) {
      if (diagnostics != nullptr) diagnostics->push_back({common::DiagnosticKind::malformed_instruction, offset, ex.what()});
      InstructionIR trap;
      trap.address = base_address + offset;
      trap.offset = offset;
      trap.size = 1;
      trap.mnemonic = "opaque";
      trap.can_reencode = false;
      trap.skip_reason = std::string("SKIP_REASON=") + ex.what();
      trap.encoding = {bytes[offset]};
      out.push_back(std::move(trap));
      ++offset;
    }
  }
  return out;
}

std::vector<std::uint8_t> reencode_instruction(const InstructionIR& instruction) {
  if (!instruction.encoding.empty()) {
    return instruction.encoding;
  }
  throw std::runtime_error("x64 reencode: instruction has no retained encoding");
}

}  // namespace vmp::arch::x64
