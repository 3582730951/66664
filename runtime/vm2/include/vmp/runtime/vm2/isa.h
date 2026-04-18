#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace vmp::runtime::vm2 {

inline constexpr std::array<std::uint8_t, 4> kVm2Magic{{'V', 'M', 'P', '2'}};
inline constexpr std::uint16_t kVm2Version = 1;
inline constexpr std::size_t kVm2GeneralRegisterCount = 32;
inline constexpr std::size_t kVm2VectorRegisterCount = 16;
inline constexpr std::size_t kVm2FloatRegisterCount = 8;
inline constexpr std::size_t kVm2PredicateCount = 8;
inline constexpr std::size_t kVm2DefaultStackSize = 128u * 1024u;
inline constexpr std::size_t kVm2KeyContextIdSize = 16u;

union Vec128 {
  struct {
    std::uint64_t lo;
    std::uint64_t hi;
  } u64;
  std::array<std::uint64_t, 2> lanes;
};
static_assert(sizeof(Vec128) == 16, "Vec128 must be 16 bytes");

enum class Opcode : std::uint16_t {
  nop = 0x1000,
  brk = 0x1001,
  ftrap = 0x1002,

  ildimm = 0x1100,
  vldimm = 0x1101,
  imov = 0x1102,

  iadd = 0x1200,
  isub = 0x1201,
  imul = 0x1202,
  idiv = 0x1203,
  imod = 0x1204,
  iand = 0x1205,
  ior = 0x1206,
  ixor = 0x1207,
  ishl = 0x1208,
  ishr = 0x1209,
  isar = 0x120A,
  ineg = 0x120B,
  inot = 0x120C,

  vadd128 = 0x1300,
  vsub128 = 0x1301,
  vmul128 = 0x1302,
  vxor128 = 0x1303,

  imemld8 = 0x1400,
  imemld16 = 0x1401,
  imemld32 = 0x1402,
  imemld64 = 0x1403,
  imemst8 = 0x1404,
  imemst16 = 0x1405,
  imemst32 = 0x1406,
  imemst64 = 0x1407,
  vmemld128 = 0x1408,
  vmemst128 = 0x1409,

  jmp = 0x1500,
  jp = 0x1501,
  jnp = 0x1502,
  blnk = 0x1503,
  bret = 0x1504,
  pcall = 0x1505,
  pret = 0x1506,

  xcall = 0x1600,
  xret = 0x1601,

  tsload = 0x1700,
  tsrelease = 0x1701,
};

enum class MemoryBase : std::uint8_t {
  sp = 0xFE,
};

constexpr bool is_valid_general_register(std::uint8_t index) noexcept {
  return index < kVm2GeneralRegisterCount;
}

constexpr bool is_valid_vector_register(std::uint8_t index) noexcept {
  return index < kVm2VectorRegisterCount;
}

constexpr bool is_valid_float_register(std::uint8_t index) noexcept {
  return index < kVm2FloatRegisterCount;
}

constexpr bool is_valid_predicate(std::uint8_t index) noexcept {
  return index < kVm2PredicateCount;
}

inline constexpr int kVm2HandlerTableIdentity = 0x56324D32;

}  // namespace vmp::runtime::vm2
