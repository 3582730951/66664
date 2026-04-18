#include "../runtime_vm1_jit/test_common.h"

#include <cstdlib>
#include <iostream>
#include <sstream>

#include <vmp/runtime/state/state.h>
#include <vmp/runtime/vm1/vm1.h>

using namespace vmp::tests::runtime_vm1_jit;

namespace {

int run_selftest_with_env(const std::filesystem::path& audit_path) {
#if defined(_WIN32)
  std::string command = "set VMP_FORCE_JIT_CAPABILITY=disallow && set VMP_AUDIT_PATH=" + audit_path.string() +
                        " && \"" VMP_LOADER_SELFTEST_PATH "\"";
#else
  std::string command = "VMP_FORCE_JIT_CAPABILITY=disallow VMP_AUDIT_PATH='" + audit_path.string() +
                        "' '" VMP_LOADER_SELFTEST_PATH "'";
#endif
  return std::system(command.c_str());
}

}  // namespace

int main() {
  auto audit_path = temp_path("loader_capability_gate_disallow", ".log");
  require(run_selftest_with_env(audit_path) == 0, "loader selftest subprocess failed");

  std::ifstream input(audit_path);
  std::ostringstream audit;
  audit << input.rdbuf();
  require(audit.str().find("jit_execmem_unavailable") != std::string::npos,
          "expected jit_execmem_unavailable audit event");

  vmp::runtime::audit::AuditWriter writer(audit_path);
  vmp::runtime::state::RuntimeState::instance().shutdown();
  require(vmp::runtime::state::RuntimeState::instance().init_once(&writer, {"linux", "test", false}),
          "runtime state init failed");
  vmp::runtime::state::RuntimeState::instance().set_jit_capability(true);

  EnvGuard backend_guard("VMP_JIT_BACKEND", "x64");
  EnvGuard path_guard("PATH", "/nonexistent");

  auto module = vmp::runtime::vm1::assemble_module_text(R"(
entry:
  ldi_u64 vr0, 321
  ret
)");

  vmp::runtime::jit::Vm1Jit::instance().reset_for_tests();
  require(vmp::runtime::jit::Vm1Jit::instance().selected_backend_name() == "off",
          "expected interpreter-only fallback when execmem and c backend are unavailable");

  vmp::runtime::vm1::Vm1Context context(module);
  vmp::runtime::vm1::Vm1Interpreter interpreter;
  const auto result = interpreter.execute(context);
  require(result.ret_int == 321, "interpreter fallback returned wrong result");
  require(vmp::runtime::jit::Vm1Jit::instance().module_entry_count(module.id()) == 0,
          "jit should remain interpreter-only under disallow gate");

  writer.flush();
  std::ifstream input_after(audit_path);
  std::ostringstream audit_after;
  audit_after << input_after.rdbuf();
  require(audit_after.str().find("jit_execmem_unavailable") != std::string::npos,
          "expected jit_execmem_unavailable audit event after fallback");

  std::cout << "loader_capability_gate_disallow OK\n";
}
