#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

#include <vmp/runtime/trusted_oracle/oracle.h>

namespace {

struct TrialSummary {
  std::size_t trials = 0;
  std::size_t accepted = 0;
};

std::array<std::uint8_t, 16> bytes16(std::uint8_t base) {
  std::array<std::uint8_t, 16> out{};
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = static_cast<std::uint8_t>(base + static_cast<std::uint8_t>(i));
  }
  return out;
}

vmp::runtime::trusted_oracle::VoteOutcome run_clean(vmp::runtime::trusted_oracle::TrustedOracle& oracle,
                                                    std::size_t) {
  vmp::runtime::trusted_oracle::MemoryMapReadings readings{};
  readings.maps_sampled = true;
  readings.smaps_sampled = true;
  readings.numa_maps_sampled = true;
  return oracle.evaluate_memory_maps(readings);
}

vmp::runtime::trusted_oracle::VoteOutcome run_frida(vmp::runtime::trusted_oracle::TrustedOracle& oracle,
                                                    std::size_t trial) {
  vmp::runtime::trusted_oracle::MemoryMapReadings readings{};
  readings.maps_sampled = true;
  readings.maps_hits = {"frida-agent-64.so"};
  readings.smaps_sampled = true;
  readings.smaps_hits = {"Name: frida-agent-64.so"};
  readings.numa_maps_sampled = true;
  if ((trial % 9u) == 0u) {
    readings.numa_maps_hits = {"frida-agent numa"};
  }
  return oracle.evaluate_memory_maps(readings);
}

vmp::runtime::trusted_oracle::VoteOutcome run_unicorn(vmp::runtime::trusted_oracle::TrustedOracle& oracle,
                                                      std::size_t trial) {
  vmp::runtime::trusted_oracle::TimeReadings readings{};
  readings.max_clock_delta_ns = 1'000'000;
  readings.counter_delta = 0;
  readings.monotonic_delta_ns = 25'000'000;
  readings.hardware_timer_sampled = true;
  readings.hardware_timer_delta_ns = ((trial % 13u) == 0u) ? 500'000 : 25'000'000;
  return oracle.evaluate_time_sources(readings);
}

vmp::runtime::trusted_oracle::VoteOutcome run_partial_hook(vmp::runtime::trusted_oracle::TrustedOracle& oracle,
                                                           std::size_t trial) {
  vmp::runtime::trusted_oracle::MemoryMapReadings readings{};
  readings.maps_sampled = true;
  readings.smaps_sampled = true;
  readings.numa_maps_sampled = true;
  const auto mod = trial % 5u;
  if (mod < 4u) {
    readings.maps_hits = {"frida-agent-64.so"};
  } else {
    readings.smaps_hits = {"Name: frida-agent-64.so"};
  }
  return oracle.evaluate_memory_maps(readings);
}

TrialSummary run_trials(const std::string& scenario, std::size_t trials) {
  TrialSummary summary{};
  summary.trials = trials;
  for (std::size_t i = 0; i < trials; ++i) {
    vmp::runtime::trusted_oracle::TrustedOracle oracle(bytes16(static_cast<std::uint8_t>(0x70u + (i % 11u))));
    vmp::runtime::trusted_oracle::VoteOutcome outcome;
    if (scenario == "clean") {
      outcome = run_clean(oracle, i);
    } else if (scenario == "frida") {
      outcome = run_frida(oracle, i);
    } else if (scenario == "unicorn") {
      outcome = run_unicorn(oracle, i);
    } else if (scenario == "partial_hook") {
      outcome = run_partial_hook(oracle, i);
    } else {
      throw std::runtime_error("unknown scenario: " + scenario);
    }
    summary.accepted += outcome.fact_value ? 1u : 0u;
  }
  return summary;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    std::string scenario = "clean";
    std::size_t trials = 120;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--scenario" && i + 1 < argc) {
        scenario = argv[++i];
      } else if (arg == "--trials" && i + 1 < argc) {
        trials = static_cast<std::size_t>(std::stoul(argv[++i]));
      } else {
        throw std::runtime_error("unknown argument: " + arg);
      }
    }

    const auto summary = run_trials(scenario, trials);
    const auto rate = summary.trials == 0 ? 0.0 : static_cast<double>(summary.accepted) / static_cast<double>(summary.trials);
    std::cout << std::fixed << std::setprecision(6)
              << "{"
              << "\"scenario\":\"" << scenario << "\","
              << "\"trials\":" << summary.trials << ","
              << "\"accepted\":" << summary.accepted << ","
              << "\"acceptance_rate\":" << rate
              << "}\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "trusted_oracle_ablation_matrix error: " << ex.what() << "\n";
    return 1;
  }
}
