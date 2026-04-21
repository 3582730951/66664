#pragma once

#include <cstdint>
#include <vector>

namespace vmp::runtime::vm1 {
class Vm1Module;
}

namespace vmp::runtime::vm2 {
class Vm2Module;
}

namespace vmp::runtime::self_mod {

struct InterlockRule {
  std::uint32_t trigger_pc = 0;
  std::uint32_t protected_pc = 0;
  std::uint32_t protected_length = 0;
  std::uint32_t expected_crc32 = 0;
};

std::uint32_t compute_interlock_crc32(const std::vector<std::uint8_t>& code,
                                      std::uint32_t protected_pc,
                                      std::uint32_t protected_length);

InterlockRule make_interlock_rule(const vmp::runtime::vm1::Vm1Module& module,
                                  std::uint32_t trigger_pc,
                                  std::uint32_t protected_pc,
                                  std::uint32_t protected_length);

InterlockRule make_interlock_rule(const vmp::runtime::vm2::Vm2Module& module,
                                  std::uint32_t trigger_pc,
                                  std::uint32_t protected_pc,
                                  std::uint32_t protected_length);

}  // namespace vmp::runtime::self_mod
