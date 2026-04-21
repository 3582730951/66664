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
        "ret\n");
    obf::attach_timing_trap_profile(module, profile());
    auto loaded = load_vm1_roundtrip(module);

    const auto audit_path = temp_path("timing_trap_vm1_baseline_ok");
    std::filesystem::remove(audit_path);
    audit::AuditWriter writer(audit_path);
    audit::ReactionDispatcher dispatcher(writer, audit::ReactionPolicy::audit_only);
    int exit_calls = 0;
    dispatcher.set_exit_fn([&exit_calls]() { ++exit_calls; });
    dispatcher.set_scheduler([](std::chrono::milliseconds, std::function<void()> fn) { fn(); });
    dispatcher.set_delay_selector([]() { return std::chrono::milliseconds(0); });

    auto counters = absolute_counters({150u, 150u, 150u, 150u, 150u, 150u, 150u});
    std::size_t next_counter = 0;
    obf::ScopedTimingTrapCounterOverride override([&]() {
      require(next_counter < counters.size(), "vm1 baseline counter stream exhausted");
      return counters[next_counter++];
    });

    vmp::runtime::vm1::Vm1Context context(loaded);
    context.audit_dispatcher = &dispatcher;
    vmp::runtime::vm1::Vm1Interpreter interpreter;
    const auto result = interpreter.execute(context);
    writer.flush();

    require(result.ret_int == 7u, "timing trap baseline should preserve exact vm1 result");
    require(exit_calls == 0, "timing trap baseline should not request exit");
    require(read_all(audit_path).find("timing_trap_triggered") == std::string::npos,
            "baseline vm1 run should not emit timing_trap_triggered");

    std::cout << "timing_trap_vm1_baseline_ok OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "timing_trap_vm1_baseline_ok failed: " << ex.what() << '\n';
    return 1;
  }
}
