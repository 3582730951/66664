#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

#include <vmp/runtime/stack_probe/probe.h>

#include "test_common.h"

int main() {
  using namespace vmp::runtime::stack_probe;
  using namespace vmp::tests::runtime_stack_probe;

  require(selector_low16(0x123456789abcdeffull) == 0xdeffu, "selector_low16 should retain low 16 bits");

  const auto audit_path = temp_path("stack_probe_selector_low16_and_maps_cache", ".log");
  std::filesystem::remove(audit_path);

  int maps_reads = 0;
  ProbeDependencies deps;
  deps.trigger_decider = [](std::uint16_t, std::uint64_t) { return true; };
  deps.frame_pointer_walker = []() {
    return std::vector<std::uintptr_t>{0x70000010u};
  };
  deps.maps_reader = [&]() {
    ++maps_reads;
    return std::string("70000000-70001000 r-xp 00000000 00:00 0\n");
  };

  StackProbeManager probe(audit_path, deps);

  ProbeRequest request;
  request.token_low16 = 0xbeefu;
  request.site = ProbeTriggerSite::dispatcher_entry;

  const auto first = probe.maybe_probe(request);
  const auto second = probe.maybe_probe(request);
  require(first.triggered && second.triggered, "forced trigger path should always probe");
  require(maps_reads == 1, "maps reader should be cached for 100ms");

  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  const auto third = probe.maybe_probe(request);
  require(third.triggered, "trigger should still fire after cache expiry");
  require(maps_reads == 2, "maps reader should refresh after cache TTL");

  std::cout << "stack_probe_selector_low16_and_maps_cache OK\n";
  return 0;
}
