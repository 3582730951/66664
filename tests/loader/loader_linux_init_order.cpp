#include <vmp/runtime/state/state.h>

#include <cstdlib>
#include <iostream>

namespace {

bool g_loader_won = false;

__attribute__((constructor(65535))) static void default_priority_ctor() {
  g_loader_won = vmp::runtime::state::RuntimeState::instance().check_flag(
      vmp::runtime::state::RuntimeFlag::loader_initialized);
}

}  // namespace

int main() {
  if (!g_loader_won) {
    std::cerr << "loader_init_order FAIL" << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "loader_init_order OK" << std::endl;
  return EXIT_SUCCESS;
}
