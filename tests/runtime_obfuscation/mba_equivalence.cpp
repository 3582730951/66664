#include "test_common.h"

#include <iostream>

using namespace vmp::tests::runtime_obfuscation;

int main() {
  try {
    auto rng = make_rng();
    for (std::size_t i = 0; i < 4096; ++i) {
      const auto x = rng();
      const auto y = rng();
      require(vmp::runtime::obfuscation::mba_add_u64(x, y) == x + y, "mba_add_u64 mismatch");
      require(vmp::runtime::obfuscation::mba_sub_u64(x, y) == x - y, "mba_sub_u64 mismatch");
      require(vmp::runtime::obfuscation::mba_mul2_u64(x) == x * 2u, "mba_mul2_u64 mismatch");
    }
    std::cout << "runtime_obfuscation_mba_equivalence OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "runtime_obfuscation_mba_equivalence failed: " << ex.what() << '\n';
    return 1;
  }
}
