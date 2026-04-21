#include <vmp/runtime/self_mod/interlock.h>

#include <stdexcept>

#include <vmp/runtime/integrity/crc32.h>
#include <vmp/runtime/vm1/vm1.h>
#include <vmp/runtime/vm2/vm2.h>

namespace vmp::runtime::self_mod {
namespace {

InterlockRule make_rule_from_code(const std::vector<std::uint8_t>& code,
                                  std::uint32_t trigger_pc,
                                  std::uint32_t protected_pc,
                                  std::uint32_t protected_length) {
  InterlockRule rule;
  rule.trigger_pc = trigger_pc;
  rule.protected_pc = protected_pc;
  rule.protected_length = protected_length;
  rule.expected_crc32 = compute_interlock_crc32(code, protected_pc, protected_length);
  return rule;
}

}  // namespace

std::uint32_t compute_interlock_crc32(const std::vector<std::uint8_t>& code,
                                      std::uint32_t protected_pc,
                                      std::uint32_t protected_length) {
  const auto begin = static_cast<std::size_t>(protected_pc);
  const auto length = static_cast<std::size_t>(protected_length);
  if (length == 0u || begin > code.size() || begin + length > code.size()) {
    throw std::runtime_error("self_mod: interlock range out of bounds");
  }
  return vmp::runtime::integrity::crc32_compute(code.data() + begin, length);
}

InterlockRule make_interlock_rule(const vmp::runtime::vm1::Vm1Module& module,
                                  std::uint32_t trigger_pc,
                                  std::uint32_t protected_pc,
                                  std::uint32_t protected_length) {
  return make_rule_from_code(module.code, trigger_pc, protected_pc, protected_length);
}

InterlockRule make_interlock_rule(const vmp::runtime::vm2::Vm2Module& module,
                                  std::uint32_t trigger_pc,
                                  std::uint32_t protected_pc,
                                  std::uint32_t protected_length) {
  return make_rule_from_code(module.code, trigger_pc, protected_pc, protected_length);
}

}  // namespace vmp::runtime::self_mod
