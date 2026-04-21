#include <iostream>

#include "test_common.h"

int main() {
  using namespace vmp::tests::runtime_timing_traps;
  namespace obf = vmp::runtime::obfuscation;

  try {
    auto vm1 = vmp::runtime::vm1::assemble_module_text("ldi_u64 vr0, 7\nret\n");
    auto vm2 = vmp::runtime::vm2::assemble_module_text("ildimm r0, 9\nbret\n");

    auto cfg = profile();
    cfg.min_cycles = 0x0102030405060708ull;
    cfg.max_cycles = 0x4142434445464748ull;
    cfg.median_cycles = 0x2122232425262728ull;
    cfg.p99_cycles = 0x3132333435363738ull;

    obf::attach_timing_trap_profile(vm1, cfg);
    obf::attach_timing_trap_profile(vm2, cfg);

    const auto vm1_bytes = vm1.serialize();
    const auto vm2_bytes = vm2.serialize();
    require(!contains_subsequence(vm1_bytes, le64(cfg.min_cycles)), "vm1 serialized timing profile leaked plaintext min_cycles");
    require(!contains_subsequence(vm1_bytes, le64(cfg.p99_cycles)), "vm1 serialized timing profile leaked plaintext p99_cycles");
    require(!contains_subsequence(vm2_bytes, le64(cfg.min_cycles)), "vm2 serialized timing profile leaked plaintext min_cycles");
    require(!contains_subsequence(vm2_bytes, le64(cfg.p99_cycles)), "vm2 serialized timing profile leaked plaintext p99_cycles");

    const auto vm1_loaded = vmp::runtime::vm1::Vm1Module::load_from_bytes(vm1_bytes);
    const auto vm2_loaded = vmp::runtime::vm2::Vm2Module::load_from_bytes(vm2_bytes);
    const auto vm1_profile = obf::decode_timing_trap_profile(vm1_loaded);
    const auto vm2_profile = obf::decode_timing_trap_profile(vm2_loaded);
    require(vm1_profile.has_value(), "vm1 timing profile missing after round-trip");
    require(vm2_profile.has_value(), "vm2 timing profile missing after round-trip");
    require(vm1_profile->min_cycles == cfg.min_cycles, "vm1 min_cycles mismatch after round-trip");
    require(vm1_profile->p99_cycles == cfg.p99_cycles, "vm1 p99 mismatch after round-trip");
    require(vm2_profile->median_cycles == cfg.median_cycles, "vm2 median mismatch after round-trip");
    require(vm2_profile->max_cycles == cfg.max_cycles, "vm2 max mismatch after round-trip");

    std::cout << "timing_trap_profile_roundtrip OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "timing_trap_profile_roundtrip failed: " << ex.what() << '\n';
    return 1;
  }
}
