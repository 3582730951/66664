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
  cfg.mutations.push_back(sm::make_vm1_mutation_rule(template_ctx, 0u, 3u, 8u, bytes32(0x61)));
  sm::attach(module, cfg);
  auto loaded = vmp::runtime::vm1::Vm1Module::load_from_bytes(module.serialize());

  const auto audit_path = temp_path("self_mod_vm1_hmac_divergence");
  std::filesystem::remove(audit_path);
  vmp::runtime::audit::AuditWriter writer(audit_path);
  bool exit_requested = false;
  auto dispatcher = make_dispatcher(writer, exit_requested);

  vmp::runtime::vm1::Vm1Context run_ctx(loaded);
  run_ctx.audit_dispatcher = &dispatcher;
  run_ctx.vr[3] = 0x55;
  (void)vmp::runtime::vm1::Vm1Interpreter{}.execute(run_ctx);
  writer.flush();

  const auto log = read_all(audit_path);
  require(log.find("bytecode_hmac_divergence") != std::string::npos,
          "divergent vm1 state should emit bytecode_hmac_divergence audit");
  require(exit_requested, "bytecode_hmac_divergence should request delayed exit");

  std::cout << "self_mod_vm1_hmac_divergence OK\n";
  return 0;
}
