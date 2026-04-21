#include "test_common.h"

#include <iostream>

int main() {
  using namespace vmp::tests::runtime_self_mod;
  namespace sm = vmp::runtime::self_mod;

  auto module = vmp::runtime::vm1::assemble_module_text(R"(
    ldi_u64 vr7, 0x0102030405060708
    ldi_u64 vr0, 7
    ret
  )");

  vmp::runtime::vm1::Vm1Context template_ctx(module);
  sm::ModuleConfig cfg;
  cfg.mutations.push_back(sm::make_vm1_mutation_rule(template_ctx, 0u, 3u, 8u, bytes32(0x31)));
  sm::attach(module, cfg);
  auto loaded = vmp::runtime::vm1::Vm1Module::load_from_bytes(module.serialize());

  std::vector<std::uint8_t> fetched_target_bytes;
  sm::ObserverHooks hooks;
  hooks.on_fetch = [&](const sm::FetchObservation& obs) {
    if (obs.domain == vmp::runtime::cryptor::VmDomain::vm1 && obs.pc >= 3u && obs.pc < 11u) {
      fetched_target_bytes.push_back(obs.runtime_byte);
    }
  };
  sm::ScopedObserverHooks scoped_hooks(hooks);

  vmp::runtime::vm1::Vm1Context run_ctx(loaded);
  const auto result = vmp::runtime::vm1::Vm1Interpreter{}.execute(run_ctx).ret_int;

  require(result == 7u, "vm1 self-mod should preserve observable result");
  require(fetched_target_bytes.size() == 8u, "vm1 self-mod should expose 8 fetched bytes in target window");
  require(!std::equal(fetched_target_bytes.begin(), fetched_target_bytes.end(), loaded.code.begin() + 3),
          "runtime fetched bytes should diverge from static bytecode dump");

  std::cout << "self_mod_vm1_runtime_fetch_mismatch OK\n";
  return 0;
}
