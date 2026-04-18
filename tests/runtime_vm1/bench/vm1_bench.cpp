#include "../test_common.h"

#include <chrono>
#include <iostream>

using namespace vmp::tests::runtime_vm1;

std::string make_nop_program(std::size_t count) {
  std::string text;
  text.reserve(count * 4 + 16);
  for (std::size_t i = 0; i < count; ++i) {
    text += "nop\n";
  }
  text += "ret\n";
  return text;
}

std::string make_add_program(std::size_t count) {
  std::string text = "ldi_u64 vr0, 0\nldi_u64 vr1, 1\n";
  text.reserve(count * 16 + 64);
  for (std::size_t i = 0; i < count; ++i) {
    text += "add vr0, vr0, vr1\n";
  }
  text += "ret\n";
  return text;
}

int main() {
  try {
    constexpr std::size_t kCount = 1000000;
    const auto nop_program = make_nop_program(kCount);
    const auto add_program = make_add_program(kCount);

    auto begin = std::chrono::steady_clock::now();
    (void)run_text(nop_program);
    auto end = std::chrono::steady_clock::now();
    auto nop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();

    begin = std::chrono::steady_clock::now();
    auto add_result = run_text(add_program);
    end = std::chrono::steady_clock::now();
    auto add_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();

    require_int(add_result.ret_int, kCount, "bench_add_result");
    std::cout << "vm1_bench nop_ms=" << nop_ms << " add_ms=" << add_ms << '\n';
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vm1_bench failed: " << ex.what() << '\n';
    return 1;
  }
}
