#include "test_common.h"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <vmp/arch/common/pc_relative.h>
#include <vmp/arch/x64/ir.h>
#include <vmp/arch/x64/x64.h>
#include <vmp/runtime/vm1/vm1.h>
#include <vmp/runtime/vm2/vm2.h>

namespace common = vmp::arch::common;
namespace x64 = vmp::arch::x64;
namespace vm1 = vmp::runtime::vm1;
namespace vm2 = vmp::runtime::vm2;

namespace {
using vmp::tests::arch::require;

struct Case {
  std::string name;
  std::vector<std::uint8_t> bytes;
  std::uint64_t address = 0;
  common::PcRelativeTarget::Kind kind = common::PcRelativeTarget::Kind::branch;
  std::uint64_t source_pc = 0;
  std::int64_t displacement = 0;
  std::uint64_t absolute = 0;
};

std::string hex_bytes(const std::vector<std::uint8_t>& bytes) {
  std::ostringstream oss;
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (i != 0) oss << ' ';
    oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(bytes[i]);
  }
  return oss.str();
}

void verify_case(const Case& tc) {
  const auto ir = x64::decode_instruction(tc.bytes, tc.address);
  require(ir.pc_relative_target.has_value(), tc.name + ": missing pc_relative_target");
  const auto& target = *ir.pc_relative_target;
  require(target.kind == tc.kind, tc.name + ": wrong kind");
  require(target.source_pc == tc.source_pc, tc.name + ": wrong source_pc");
  require(target.displacement == tc.displacement, tc.name + ": wrong displacement");
  require(target.computed_absolute == tc.absolute, tc.name + ": wrong computed_absolute");
  require(x64::reencode_instruction(ir) == tc.bytes, tc.name + ": reencode mismatch");

  switch (tc.bytes.size() == 2 ? 8 : 32) {
    case 8:
      require(common::encode_rel8(target.source_pc, target.computed_absolute) == target.displacement,
              tc.name + ": rel8 helper mismatch");
      break;
    default:
      require(common::encode_rel32(target.source_pc, target.computed_absolute) == target.displacement,
              tc.name + ": rel32 helper mismatch");
      break;
  }
}

std::uint64_t run_vm2(const vm2::Vm2Module& module) {
  vm2::Vm2Context ctx(module);
  return vm2::Vm2Interpreter{}.execute(ctx).ret_int;
}

std::uint64_t parse_loaded_immediate(const std::string& text, const std::string& opcode_prefix) {
  const auto pos = text.find(opcode_prefix);
  require(pos != std::string::npos, "expected immediate load opcode not found");
  const auto start = pos + opcode_prefix.size();
  const auto end = text.find_first_of("\r\n", start);
  return static_cast<std::uint64_t>(std::stoull(text.substr(start, end - start)));
}

void verify_integration_vm1() {
  common::FunctionView view;
  view.base_addr = 0x4000;
  view.cc = common::CallingConvention::sysv_x64;
  view.endian = common::ArchEndianness::little;
  view.code = {0x48, 0x8D, 0x05, 0xF9, 0xFF, 0xFF, 0xFF, 0xC3};  // lea rax, [rip-7]; ret

  x64::X64Lifter lifter(common::TargetDomain::vm1);
  auto lifted = lifter.lift(view);
  require(lifted.ok(), "x64 vm1 lea-rip lift failed");
  const auto text = vm1::disassemble_module(lifted.module);
  const auto expected = parse_loaded_immediate(text, "ldi_u64 vr30, ");
  require(vmp::tests::arch::run_vm1_abi(lifted.module, view.cc, {}) == expected,
          "x64 vm1 lea-rip returned unexpected value");
  require(text.find("ldi_u64 vr30, ") != std::string::npos,
          "x64 vm1 disassembly did not contain label/immediate materialization");
}

void verify_integration_vm2() {
  common::FunctionView view;
  view.base_addr = 0x5000;
  view.cc = common::CallingConvention::sysv_x64;
  view.endian = common::ArchEndianness::little;
  view.code = {0x48, 0x8D, 0x05, 0xF9, 0xFF, 0xFF, 0xFF, 0xC3};

  x64::X64Lifter lifter(common::TargetDomain::vm2);
  auto lifted = lifter.lift(view);
  require(lifted.ok(), "x64 vm2 lea-rip lift failed");
  require(lifted.vm2_module.has_value(), "x64 vm2 module missing");
  const auto text = vm2::disassemble_module(*lifted.vm2_module);
  const auto expected = parse_loaded_immediate(text, "ildimm r30, ");
  require(run_vm2(*lifted.vm2_module) == expected, "x64 vm2 lea-rip returned unexpected value");
}

std::vector<Case> build_cases() {
  return {
      {"jmp_rel32_fwd", {0xE9, 0x10, 0x00, 0x00, 0x00}, 0x1000,
       common::PcRelativeTarget::Kind::branch, 0x1005, 0x10, 0x1015},
      {"jmp_rel8_back", {0xEB, 0xF0}, 0x1100,
       common::PcRelativeTarget::Kind::branch, 0x1102, -16, 0x10F2},
      {"je_rel8", {0x74, 0x7F}, 0x1200,
       common::PcRelativeTarget::Kind::branch, 0x1202, 127, 0x1281},
      {"jne_rel32", {0x0F, 0x85, 0x34, 0x12, 0x00, 0x00}, 0x1300,
       common::PcRelativeTarget::Kind::branch, 0x1306, 0x1234, 0x253A},
      {"call_rel32", {0xE8, 0x08, 0x00, 0x00, 0x00}, 0x1400,
       common::PcRelativeTarget::Kind::call, 0x1405, 8, 0x140D},
      {"lea_rip_pos", {0x48, 0x8D, 0x05, 0x78, 0x56, 0x34, 0x12}, 0x1500,
       common::PcRelativeTarget::Kind::address_materialize, 0x1507, 0x12345678ll, 0x12346B7F},
      {"lea_rip_neg", {0x48, 0x8D, 0x0D, 0xF9, 0xFF, 0xFF, 0xFF}, 0x1600,
       common::PcRelativeTarget::Kind::address_materialize, 0x1607, -7, 0x1600},
      {"mov_rip_load", {0x48, 0x8B, 0x05, 0x01, 0x00, 0x00, 0x00}, 0x1700,
       common::PcRelativeTarget::Kind::load, 0x1707, 1, 0x1708},
      {"mov_rip_store", {0x48, 0x89, 0x05, 0x01, 0x00, 0x00, 0x00}, 0x1800,
       common::PcRelativeTarget::Kind::store, 0x1807, 1, 0x1808},
      {"jmp_table_rip", {0xFF, 0x25, 0x01, 0x00, 0x00, 0x00}, 0x1900,
       common::PcRelativeTarget::Kind::indirect_jump_via_table, 0x1906, 1, 0x1907},
      {"call_table_rip", {0xFF, 0x15, 0x01, 0x00, 0x00, 0x00}, 0x1A00,
       common::PcRelativeTarget::Kind::indirect_jump_via_table, 0x1A06, 1, 0x1A07},
      {"movdqa_rip_load", {0x66, 0x0F, 0x6F, 0x05, 0x20, 0x00, 0x00, 0x00}, 0x1B00,
       common::PcRelativeTarget::Kind::load, 0x1B08, 0x20, 0x1B28},
      {"movdqa_rip_store", {0x66, 0x0F, 0x7F, 0x05, 0x20, 0x00, 0x00, 0x00}, 0x1C00,
       common::PcRelativeTarget::Kind::store, 0x1C08, 0x20, 0x1C28},
      {"vmovdqa_rip_load", {0xC5, 0xFD, 0x6F, 0x05, 0x20, 0x00, 0x00, 0x00}, 0x1D00,
       common::PcRelativeTarget::Kind::load, 0x1D08, 0x20, 0x1D28},
      {"vmovdqa_rip_store", {0xC5, 0xFD, 0x7F, 0x05, 0x20, 0x00, 0x00, 0x00}, 0x1E00,
       common::PcRelativeTarget::Kind::store, 0x1E08, 0x20, 0x1E28},
  };
}
}  // namespace

int main() {
  try {
    for (const auto& tc : build_cases()) {
      verify_case(tc);
    }
    verify_integration_vm1();
    verify_integration_vm2();
    std::cout << "pc_relative_x64 OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "pc_relative_x64 failed: " << ex.what() << '\n';
    return 1;
  }
}
