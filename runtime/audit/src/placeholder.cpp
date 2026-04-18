#include <vmp/runtime/audit/placeholder.h>

#include <mutex>

namespace vmp::runtime::audit {
namespace {

std::once_flag g_placeholder_once;

}  // namespace

void initialize_placeholder_hook_once() noexcept {
  std::call_once(g_placeholder_once, []() noexcept { vm_placeholder_analysis_awareness_hook(); });
}

}  // namespace vmp::runtime::audit

extern "C" void vm_placeholder_analysis_awareness_hook(void) {}
