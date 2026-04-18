#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace vmp::runtime::vm1 {

inline constexpr std::array<std::uint8_t, 4> kVm1Magic{{'V', 'M', '1', 'B'}};
inline constexpr std::uint16_t kVm1Version = 1;
inline constexpr std::size_t kVm1GeneralRegisterCount = 32;
inline constexpr std::size_t kVm1FloatRegisterCount = 4;
inline constexpr std::size_t kVm1DefaultStackSize = 64u * 1024u;

enum class Opcode : std::uint8_t {
  nop = 0x00,
  breakpoint = 0x01,
  trap = 0x02,
  ldi64 = 0x03,
  ldi_u64 = 0x04,
  ldi_f64 = 0x05,
  mov = 0x06,
  add = 0x10,
  sub = 0x11,
  mul = 0x12,
  div = 0x13,
  mod = 0x14,
  bit_and = 0x15,
  bit_or = 0x16,
  bit_xor = 0x17,
  shl = 0x18,
  shr = 0x19,
  sar = 0x1A,
  neg = 0x1B,
  bit_not = 0x1C,
  load_mem8 = 0x20,
  load_mem16 = 0x21,
  load_mem32 = 0x22,
  load_mem64 = 0x23,
  store_mem8 = 0x24,
  store_mem16 = 0x25,
  store_mem32 = 0x26,
  store_mem64 = 0x27,
  jmp = 0x30,
  jeq = 0x31,
  jne = 0x32,
  jlt = 0x33,
  jle = 0x34,
  jgt = 0x35,
  jge = 0x36,
  call = 0x37,
  ret = 0x38,
  domain_call = 0x39,
  domain_ret = 0x3A,
  load_transient_string = 0x40,
  release_transient_string = 0x41,
};

enum class ConstKind : std::uint8_t {
  none = 0,
  transient_string = 1,
};

enum class MemoryBase : std::uint8_t {
  stack_pointer = 0xFF,
};

constexpr bool is_valid_general_register(std::uint8_t index) noexcept {
  return index < kVm1GeneralRegisterCount;
}

constexpr bool is_valid_float_register(std::uint8_t index) noexcept {
  return index < kVm1FloatRegisterCount;
}

}  // namespace vmp::runtime::vm1
