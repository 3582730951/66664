#include "test_common.h"

#include <iostream>

using namespace vmp::tests::runtime_obfuscation;

int main() {
  try {
    auto rng = make_rng();
    for (std::size_t i = 0; i < 4096; ++i) {
      const auto x = rng();
      require(vmp::runtime::obfuscation::opaque_even_product_predicate(x),
              "opaque_even_product_predicate must remain true");
    }
    std::cout << "runtime_obfuscation_opaque_predicates OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "runtime_obfuscation_opaque_predicates failed: " << ex.what() << '\n';
    return 1;
  }
}
