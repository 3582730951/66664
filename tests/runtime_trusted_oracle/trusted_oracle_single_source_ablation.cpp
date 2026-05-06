#include "test_common.h"

#include <cstdlib>
#include <iostream>

#include <vmp/runtime/trusted_oracle/oracle.h>

using namespace vmp::tests::runtime_trusted_oracle;

namespace {

void set_single_source_mode(bool enabled) {
#if defined(_WIN32)
  if (enabled) {
    _putenv_s("VMP_ORACLE_SINGLE_SOURCE", "1");
  } else {
    _putenv_s("VMP_ORACLE_SINGLE_SOURCE", "");
  }
#else
  if (enabled) {
    ::setenv("VMP_ORACLE_SINGLE_SOURCE", "1", 1);
  } else {
    ::unsetenv("VMP_ORACLE_SINGLE_SOURCE");
  }
#endif
}

}  // namespace

int main() {
  const auto audit_path = temp_path("trusted_oracle_single_source", ".log");
  vmp::runtime::trusted_oracle::TrustedOracle oracle(bytes16(0x61), audit_path);

  vmp::runtime::trusted_oracle::MemoryMapReadings partial{};
  partial.maps_sampled = true;
  partial.maps_hits = {"7f0000-7f1000 r-xp 00000000 00:00 0 frida-agent-64.so"};
  partial.smaps_sampled = true;
  partial.smaps_hits = {};
  partial.numa_maps_sampled = true;
  partial.numa_maps_hits = {};

  set_single_source_mode(false);
  const auto baseline = oracle.evaluate_memory_maps(partial);
  require(!baseline.fact_value, "baseline oracle must reject a single polluted source");
  require(baseline.threshold >= 2, "baseline oracle should still require multi-source agreement");

  set_single_source_mode(true);
  const auto degraded = oracle.evaluate_memory_maps(partial);
  set_single_source_mode(false);

  require(degraded.fact_value, "single-source ablation must accept the first polluted source");
  require(degraded.threshold == 1, "single-source ablation should collapse threshold to 1");
  require(degraded.sampled_count == 1, "single-source ablation should only trust the first sampled source");
  require(degraded.positive_count == 1, "single-source ablation should keep the first positive source");
  require(degraded.negative_count == 0, "single-source ablation should ignore later negative sources");

  std::cout << "trusted_oracle_single_source_ablation OK\n";
  return 0;
}
