#include "test_common.h"

#include <vmp/runtime/state/hot_recorder.h>

#include <iostream>
#include <thread>
#include <vector>

using namespace vmp::tests::runtime_state;

int main() {
  vmp::runtime::state::HotRecorder recorder;
  std::vector<std::thread> threads;
  constexpr int kThreads = 8;
  constexpr int kEvents = 100000;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t]() {
      for (int i = 0; i < kEvents; ++i) {
        recorder.record_function_entry(1, static_cast<std::uint32_t>(t));
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  const auto snapshot = recorder.snapshot();
  std::uint64_t total = 0;
  for (const auto& [_, value] : snapshot.function_hits) {
    total += value;
  }
  require(total == static_cast<std::uint64_t>(kThreads) * kEvents, "unexpected total count");
  std::cout << "hot_recorder_concurrency_8_threads OK\n";
}
