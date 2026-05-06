#include "test_common.h"

#include <iostream>
#include <sstream>

using namespace vmp::tests::runtime_rolling_opcode;
namespace roll1 = vmp::runtime::cryptor::vm1;

int main() {
  auto& registry = vmp::runtime::cryptor::RollingOpcodeRegistry::instance();
  registry.reset_for_tests();

  std::ostringstream program;
  program << "entry:\n";
  for (int i = 0; i < 420; ++i) {
    program << "  ldi_u64 vr0, " << i << "\n";
  }
  program << "  ret\n";

  auto module = make_vm1_module(program.str(), 0x33);
  auto descriptor = roll1::describe_module(module);
  registry.ensure_module(descriptor);
  registry.rotate_module(descriptor, vmp::runtime::cryptor::RotationReason::key_rotation, vec16(0x90));

  const auto epoch = registry.current_epoch_id(descriptor);
  require(epoch == 1u, "expected first rotated epoch to be 1");
  require(registry.materialized_page_count(descriptor, epoch) == 0u,
          "new epoch should start with zero materialized pages");

  (void)registry.fetch_decoded_byte(descriptor, 0u);
  require(registry.materialized_page_count(descriptor, epoch) == 1u,
          "first fetch should materialize exactly one page");

  (void)registry.fetch_decoded_byte(descriptor, 128u);
  require(registry.materialized_page_count(descriptor, epoch) == 1u,
          "same-page fetch must not rematerialize");

  const auto second_page_pc = static_cast<std::size_t>(4096u);
  require(second_page_pc < module.code.size(), "test module must span at least two pages");
  (void)registry.fetch_decoded_byte(descriptor, second_page_pc);
  require(registry.materialized_page_count(descriptor, epoch) == 2u,
          "second-page fetch should materialize a second page");

  const auto encoded = registry.debug_forward_storage_for_epoch(descriptor, epoch);
  require(encoded.size() == module.code.size(), "debug storage size mismatch after lazy materialization");

  std::cout << "rolling_opcode_lazy_page_materialization OK\n";
  return 0;
}
