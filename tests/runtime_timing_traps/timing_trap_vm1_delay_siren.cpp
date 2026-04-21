#include <chrono>
#include <filesystem>
#include <iostream>

#include <vmp/runtime/audit/audit.h>
#include <vmp/runtime/audit/reaction.h>

#include "test_common.h"

int main() {
  using namespace vmp::tests::runtime_timing_traps;
  namespace audit = vmp::runtime::audit;
  namespace obf = vmp::runtime::obfuscation;

  try {
    auto module = vmp::runtime::vm1::assemble_module_text(
        "ldi_u64 vr0, 1\n"
        "ldi_u64 vr1, 2\n"
        "add vr0, vr0, vr1\n"
        "add vr0, vr0, vr1\n"
        "add vr0, vr0, vr1\n"
        "add vr0, vr0, vr1\n"
        "add vr0, vr0, vr1\n"
        "ret\n");
    obf::attach_timing_trap_profile(module, profile());
    auto loaded = load_vm1_roundtrip(module);

    const auto audit_path = temp_path("timing_trap_vm1_delay_siren");
    std::filesystem::remove(audit_path);
    audit::AuditWriter writer(audit_path);
    audit::ReactionDispatcher dispatcher(writer, audit::ReactionPolicy::audit_only);
    int exit_calls = 0;
    dispatcher.set_exit_fn([&exit_calls]() { ++exit_calls; });
    dispatcher.set_scheduler([](std::chrono::milliseconds, std::function<void()> fn) { fn(); });
    dispatcher.set_delay_selector([]() { return std::chrono::milliseconds(0); });

    auto counters = absolute_counters({150u, 5000u, 5000u, 5000u, 150u, 150u, 150u, 150u});
    std::size_t next_counter = 0;
    obf::ScopedTimingTrapCounterOverride override([&]() {
      require(next_counter < counters.size(), "vm1 delayed counter stream exhausted");
      return counters[next_counter++];
    });

    vmp::runtime::vm1::Vm1Context context(loaded);
    context.audit_dispatcher = &dispatcher;
    vmp::runtime::vm1::Vm1Interpreter interpreter;
    const auto result = interpreter.execute(context);
    writer.flush();

    require(result.ret_int != 11u, "vm1 delayed timing anomaly should poison final result");
    const auto audit_text = read_all(audit_path);
    require(audit_text.find("timing_trap_triggered") != std::string::npos,
            "vm1 delayed timing anomaly should audit timing_trap_triggered");
    require(audit_text.find("delta=") != std::string::npos, "timing trap note should include observed delta");
    require(audit_text.find("p99=") != std::string::npos, "timing trap note should include baseline p99");
    require(exit_calls == 0, "timing trap siren should not terminate the process");

    std::cout << "timing_trap_vm1_delay_siren OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "timing_trap_vm1_delay_siren failed: " << ex.what() << '\n';
    return 1;
  }
}
