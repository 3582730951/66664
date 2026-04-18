#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <vmp/runtime/audit/audit.h>
#include <vmp/runtime/audit/reaction.h>
#include <vmp/runtime/bridge/bridge.h>
#include <vmp/runtime/jit/vm1_jit.h>
#include <vmp/runtime/jit/vm2_jit.h>
#include <vmp/runtime/vm1/vm1.h>
#include <vmp/runtime/vm2/vm2.h>

namespace audit = vmp::runtime::audit;
namespace bridge = vmp::runtime::bridge;
namespace jit = vmp::runtime::jit;
namespace vm1 = vmp::runtime::vm1;
namespace vm2 = vmp::runtime::vm2;
using Clock = std::chrono::steady_clock;

#if defined(_WIN32)
static void set_env_var(const char* name, const char* value) { _putenv_s(name, value); }
#else
static void set_env_var(const char* name, const char* value) { ::setenv(name, value, 1); }
#endif

static std::uint64_t native_hot_loop(std::uint64_t iters) {
  std::uint64_t acc = 0;
  for (std::uint64_t i = 0; i < iters; ++i) acc += i ^ 0x5au;
  return acc;
}

static std::uint64_t time_vm1(const std::string& backend, int rounds) {
  set_env_var("VMP_JIT_BACKEND", backend.c_str());
  jit::Vm1Jit::instance().reset_for_tests();
  auto module = vm1::assemble_module_text(R"(
entry:
  ldi_u64 vr0, 0
  ldi_u64 vr1, 1
  ldi_u64 vr2, 5000
loop:
  add vr0, vr0, vr1
  jlt vr0, vr2, @loop
  ret
)");
  vm1::Vm1Interpreter interp;
  auto begin = Clock::now();
  for (int i = 0; i < rounds; ++i) {
    vm1::Vm1Context ctx(module);
    const auto result = interp.execute(ctx);
    if (result.ret_int != 5000) throw std::runtime_error("vm1 benchmark result mismatch");
  }
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count();
}

static std::uint64_t time_vm2(const std::string& backend, int rounds) {
  set_env_var("VMP_JIT_BACKEND", backend.c_str());
  jit::Vm2Jit::instance().reset_for_tests();
  auto module = vm2::assemble_module_text(R"(
  ildimm r0, 7
  bret
)");
  vm2::Vm2Interpreter interp;
  auto begin = Clock::now();
  for (int i = 0; i < rounds; ++i) {
    vm2::Vm2Context ctx(module);
    const auto result = interp.execute(ctx);
    if (result.ret_int != 7) throw std::runtime_error("vm2 benchmark result mismatch");
  }
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count();
}

static std::uint64_t measure_cross_domain() {
  bridge::BridgeRegistry registry;
  registry.register_native(77, [](const bridge::DomainCallArgs& args) {
    return bridge::DomainCallResult{args.ints.at(0) + 1, 0.0, 0};
  });
  auto module = vm1::assemble_module_text(R"(
entry:
  ldi_u64 vr0, 0
  ldi_u64 vr1, 1
  ldi_u64 vr2, 1024
loop:
  domain_call native, 77, 1
  jlt vr0, vr2, @loop
  ret
)");
  vm1::Vm1Interpreter interp;
  auto begin = Clock::now();
  vm1::Vm1Context ctx(module);
  ctx.bridge_registry = &registry;
  const auto result = interp.execute(ctx);
  auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count();
  if (result.ret_int != 1024) throw std::runtime_error("cross-domain benchmark result mismatch");
  return elapsed;
}

static double measure_key_rotation_hit_rate() {
  set_env_var("VMP_JIT_BACKEND", "c");
  jit::Vm1Jit::instance().reset_for_tests();
  auto module = vm1::assemble_module_text(R"(
entry:
  ldi_u64 vr0, 7
  ret
)");
  vm1::Vm1Interpreter interp;
  for (int i = 0; i < 12; ++i) {
    vm1::Vm1Context ctx(module);
    (void)interp.execute(ctx);
    if ((i + 1) % 3 == 0) {
      jit::Vm1Jit::instance().invalidate_module(module.id());
    }
  }
  const auto stats = jit::Vm1Jit::instance().entry_stats(module.id(), module.entry_pc);
  return 12.0 == 0.0 ? 0.0 : static_cast<double>(stats.hit_count) / 12.0;
}

static std::uint64_t measure_audit(bool enabled) {
  auto module = vm1::assemble_module_text("breakpoint\nldi_u64 vr0, 1\nret\n");
  vm1::Vm1Interpreter interp;
  std::unique_ptr<audit::AuditWriter> writer;
  std::unique_ptr<audit::ReactionDispatcher> dispatcher;
  std::filesystem::path audit_path = std::filesystem::temp_directory_path() / "vmp_final_matrix_perf_audit.log";
  if (enabled) {
    writer = std::make_unique<audit::AuditWriter>(audit_path);
    dispatcher = std::make_unique<audit::ReactionDispatcher>(*writer, audit::ReactionPolicy::audit_only);
  }
  auto begin = Clock::now();
  for (int i = 0; i < 200; ++i) {
    vm1::Vm1Context ctx(module);
    ctx.audit_dispatcher = dispatcher.get();
    const auto result = interp.execute(ctx);
    if (result.ret_int != 1) throw std::runtime_error("audit benchmark mismatch");
  }
  if (writer) writer->flush();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count();
}

int main(int argc, char** argv) {
  std::filesystem::path output;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--output" && i + 1 < argc) {
      output = argv[++i];
    } else {
      std::cerr << "usage: final_matrix_perf_helper --output <path>\n";
      return 2;
    }
  }
  if (output.empty()) {
    std::cerr << "output path is required\n";
    return 2;
  }

  const auto vm1_interpret = time_vm1("off", 6);
  const auto vm1_jit = time_vm1("c", 6);
  const auto vm2_interpret = time_vm2("off", 4);
  const auto vm2_jit = time_vm2("c", 4);
  const auto native_loop = native_hot_loop(5000);
  const auto cross_domain = measure_cross_domain();
  const auto key_rotation_hit_rate = measure_key_rotation_hit_rate();
  const auto audit_off = measure_audit(false);
  const auto audit_on = measure_audit(true);

  nlohmann::json report = {
      {"vm1_interpret_ns", vm1_interpret},
      {"vm1_jit_ns", vm1_jit},
      {"vm2_interpret_ns", vm2_interpret},
      {"vm2_jit_ns", vm2_jit},
      {"cross_domain_overhead_ns", cross_domain},
      {"jit_cache_hit_rate_key_rotation", key_rotation_hit_rate},
      {"audit_off_ns", audit_off},
      {"audit_on_ns", audit_on},
      {"native_loop_checksum", native_loop},
  };

  std::ofstream out(output);
  out << report.dump(2) << '\n';
  std::cout << "perf bench OK\n";
  return 0;
}
