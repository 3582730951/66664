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
    auto module = vmp::runtime::vm2::assemble_module_text(
        "ildimm r0, 1\n"
        "ildimm r1, 3\n"
        "iadd r0, r0, r1\n"
        "iadd r0, r0, r1\n"
        "iadd r0, r0, r1\n"
        "iadd r0, r0, r1\n"
        "bret\n");
    obf::attach_timing_trap_profile(module, profile());
    auto loaded = load_vm2_roundtrip(module);

    const auto audit_path = temp_path("timing_trap_vm2_fast_siren");
    std::filesystem::remove(audit_path);
    audit::AuditWriter writer(audit_path);
    audit::ReactionDispatcher dispatcher(writer, audit::ReactionPolicy::audit_only);
    int exit_calls = 0;
    dispatcher.set_exit_fn([&exit_calls]() { ++exit_calls; });
    dispatcher.set_scheduler([](std::chrono::milliseconds, std::function<void()> fn) { fn(); });
    dispatcher.set_delay_selector([]() { return std::chrono::milliseconds(0); });

    auto counters = absolute_counters({150u, 5u, 5u, 5u, 150u, 150u, 150u});
    std::size_t next_counter = 0;
    obf::ScopedTimingTrapCounterOverride override([&]() {
      require(next_counter < counters.size(), "vm2 fast counter stream exhausted");
      return counters[next_counter++];
    });

    vmp::runtime::vm2::Vm2Context context(loaded);
    context.audit_dispatcher = &dispatcher;
    vmp::runtime::vm2::Vm2Interpreter interpreter;
    const auto result = interpreter.execute(context);
    writer.flush();

    require(result.ret_int != 13u, "vm2 fast timing anomaly should poison final result");
    const auto audit_text = read_all(audit_path);
    require(audit_text.find("timing_trap_triggered") != std::string::npos,
            "vm2 fast timing anomaly should audit timing_trap_triggered");
    require(audit_text.find("min=") != std::string::npos, "timing trap note should include baseline min");
    require(exit_calls == 0, "timing trap siren should never quick_exit");

    std::cout << "timing_trap_vm2_fast_siren OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "timing_trap_vm2_fast_siren failed: " << ex.what() << '\n';
    return 1;
  }
}
