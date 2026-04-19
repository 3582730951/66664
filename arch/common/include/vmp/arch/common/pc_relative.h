#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace vmp::arch::common {

struct PcRelativeTarget {
  enum class Kind : std::uint8_t {
    branch,
    call,
    load,
    store,
    address_materialize,
    indirect_jump_via_table,
  };

  std::uint64_t source_pc = 0;
  std::int64_t displacement = 0;
  std::uint64_t computed_absolute = 0;
  Kind kind = Kind::branch;
};

struct Label {
  std::string name;
  std::optional<std::uint64_t> resolved_vm_pc;
};

class PcRelativeRangeError : public std::out_of_range {
 public:
  using std::out_of_range::out_of_range;
};

inline std::int64_t checked_pc_delta(std::uint64_t source_pc, std::uint64_t target_pc) {
  const auto source = static_cast<std::int64_t>(source_pc);
  const auto target = static_cast<std::int64_t>(target_pc);
  return target - source;
}

inline std::int64_t checked_range(std::int64_t value,
                                  std::int64_t min_value,
                                  std::int64_t max_value,
                                  const char* label) {
  if (value < min_value || value > max_value) {
    throw PcRelativeRangeError(std::string(label) + " displacement out of range");
  }
  return value;
}

inline std::int64_t encode_rel8(std::uint64_t source_pc, std::uint64_t target_pc) {
  return checked_range(checked_pc_delta(source_pc, target_pc), -128, 127, "rel8");
}

inline std::int64_t encode_rel16(std::uint64_t source_pc, std::uint64_t target_pc) {
  return checked_range(checked_pc_delta(source_pc, target_pc), -32768, 32767, "rel16");
}

inline std::int64_t encode_rel32(std::uint64_t source_pc, std::uint64_t target_pc) {
  return checked_range(checked_pc_delta(source_pc, target_pc),
                       std::numeric_limits<std::int32_t>::min(),
                       std::numeric_limits<std::int32_t>::max(),
                       "rel32");
}

inline std::int64_t encode_pc_page(std::uint64_t source_pc, std::uint64_t target_pc) {
  const auto source_page = static_cast<std::int64_t>(source_pc & ~0xFFFull);
  const auto target_page = static_cast<std::int64_t>(target_pc & ~0xFFFull);
  const auto page_delta = (target_page - source_page) / 4096;
  checked_range(page_delta, -(1ll << 20), (1ll << 20) - 1, "pc_page");
  return page_delta * 4096;
}

inline PcRelativeTarget make_pc_relative_target(std::uint64_t source_pc,
                                                std::int64_t displacement,
                                                PcRelativeTarget::Kind kind) {
  return PcRelativeTarget{
      source_pc,
      displacement,
      static_cast<std::uint64_t>(static_cast<std::int64_t>(source_pc) + displacement),
      kind,
  };
}

inline PcRelativeTarget make_pc_page_target(std::uint64_t source_pc,
                                            std::uint64_t target_pc,
                                            PcRelativeTarget::Kind kind) {
  const auto displacement = encode_pc_page(source_pc, target_pc);
  return PcRelativeTarget{
      source_pc,
      displacement,
      static_cast<std::uint64_t>(static_cast<std::int64_t>(source_pc & ~0xFFFull) + displacement),
      kind,
  };
}

}  // namespace vmp::arch::common
