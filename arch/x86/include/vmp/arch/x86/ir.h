#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <vmp/arch/common/lifting.h>
#include <vmp/arch/common/pc_relative.h>

namespace vmp::arch::x86 {

enum class OperandKind : std::uint8_t {
  none = 0,
  reg,
  imm,
  mem,
  fp_reg,
  simd_reg,
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

struct MemoryAddress {
  bool valid = false;
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
  std::uint32_t flags = 0;
  std::uint64_t relative_target = 0;
  bool has_relative_target = false;
  std::optional<vmp::arch::common::PcRelativeTarget> pc_relative_target;
  bool can_reencode = false;
  std::string skip_reason;
  std::vector<std::uint8_t> encoding;
};

InstructionIR decode_instruction(const std::vector<std::uint8_t>& bytes, std::uint64_t address);
std::vector<InstructionIR> decode_stream(const std::vector<std::uint8_t>& bytes,
                                         std::uint64_t base_address,
                                         std::vector<vmp::arch::common::Diagnostic>* diagnostics = nullptr);
std::vector<std::uint8_t> reencode_instruction(const InstructionIR& instruction);

}  // namespace vmp::arch::x86
