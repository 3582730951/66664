#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <vmp/arch/common/lifting.h>

namespace vmp::arch::x64 {

enum class OperandKind : std::uint8_t {
  none = 0,
  reg,
  imm,
  mem,
  fp_reg,
  simd_reg,
  predicate,
  system,
  label,
};

enum class ConditionCode : std::uint8_t {
  none = 0xFF,
  o = 0,
  no = 1,
  b = 2,
  ae = 3,
  eq = 4,
  ne = 5,
  be = 6,
  a = 7,
  s = 8,
  ns = 9,
  p = 10,
  np = 11,
  lt = 12,
  ge = 13,
  le = 14,
  gt = 15,
};

enum PrefixFlags : std::uint32_t {
  prefix_none = 0,
  prefix_66 = 1u << 0,
  prefix_67 = 1u << 1,
  prefix_lock = 1u << 2,
  prefix_repne = 1u << 3,
  prefix_rep = 1u << 4,
  prefix_seg_cs = 1u << 5,
  prefix_seg_ss = 1u << 6,
  prefix_seg_ds = 1u << 7,
  prefix_seg_es = 1u << 8,
  prefix_seg_fs = 1u << 9,
  prefix_seg_gs = 1u << 10,
  prefix_rex = 1u << 11,
  prefix_rex_w = 1u << 12,
  prefix_rex_r = 1u << 13,
  prefix_rex_x = 1u << 14,
  prefix_rex_b = 1u << 15,
  prefix_vex = 1u << 16,
  prefix_evex = 1u << 17,
};

struct MemoryAddress {
  bool valid = false;
  bool rip_relative = false;
  std::uint8_t base = 0xFFu;
  std::uint8_t index = 0xFFu;
  std::uint8_t scale = 1u;
  std::int64_t displacement = 0;
};

struct InstructionIR {
  std::uint64_t address = 0;
  std::size_t offset = 0;
  std::size_t size = 0;
  std::string mnemonic;
  std::vector<std::string> operands;
  std::vector<OperandKind> operand_kinds;
  std::vector<std::uint8_t> operand_sizes;
  std::vector<std::uint64_t> immediate_values;
  ConditionCode condition = ConditionCode::none;
  MemoryAddress memory{};
  std::uint32_t flags = prefix_none;
  std::uint64_t relative_target = 0;
  bool has_relative_target = false;
  bool can_reencode = false;
  std::string skip_reason;
  std::vector<std::uint8_t> encoding;
};

InstructionIR decode_instruction(const std::vector<std::uint8_t>& bytes, std::uint64_t address);
std::vector<InstructionIR> decode_stream(const std::vector<std::uint8_t>& bytes,
                                         std::uint64_t base_address,
                                         std::vector<vmp::arch::common::Diagnostic>* diagnostics = nullptr);
std::vector<std::uint8_t> reencode_instruction(const InstructionIR& instruction);

}  // namespace vmp::arch::x64
