#include "test_common.h"

#include <iomanip>
#include <iostream>
#include <sstream>

int main() {
  using namespace vmp::tests::runtime_self_mod;
  namespace sm = vmp::runtime::self_mod;

  std::ostringstream program;
  program << ".keyctx 0x";
  for (auto byte : bytes16(0x90)) {
    program << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
  }
  program << R"(
entry:
  ildimm r0, 9
  bret
func_a:
  ildimm r5, 0x1111111111111111
  bret
)";

  auto module = vmp::runtime::vm2::assemble_module_text(program.str());
  const std::uint32_t entry_pc = 0u;
  const std::uint32_t func_a_pc = 13u;
  const std::uint32_t func_len = 13u;

  sm::ModuleConfig cfg;
  cfg.interlocks.push_back(sm::make_interlock_rule(module, entry_pc, func_a_pc, func_len));
  cfg.interlocks.push_back(sm::make_interlock_rule(module, func_a_pc, entry_pc, func_len));
  sm::attach(module, cfg);
  auto loaded = vmp::runtime::vm2::Vm2Module::load_from_bytes(module.serialize());
  loaded.code[func_a_pc + 3u] ^= 0x5au;

  const auto audit_path = temp_path("self_mod_vm2_interlock_checksum_mismatch");
  std::filesystem::remove(audit_path);
  vmp::runtime::audit::AuditWriter writer(audit_path);
  bool exit_requested = false;
  auto dispatcher = make_dispatcher(writer, exit_requested);

  vmp::runtime::vm2::Vm2Context ctx(loaded);
  ctx.audit_dispatcher = &dispatcher;
  (void)vmp::runtime::vm2::Vm2Interpreter{}.execute(ctx);
  writer.flush();

  const auto log = read_all(audit_path);
  require(log.find("interlock_checksum_mismatch") != std::string::npos,
          "tampering peer function should emit interlock_checksum_mismatch");
  require(exit_requested, "interlock mismatch should request delayed exit");

  std::cout << "self_mod_vm2_interlock_checksum_mismatch OK\n";
  return 0;
}
