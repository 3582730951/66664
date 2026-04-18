#include <vmp/bindings/cpp/plugin.h>

namespace vmp::bindings::cpp {

const char* PluginFacade::status() const noexcept {
#if defined(VMP_BINDINGS_CPP_HAVE_CLANG_PLUGIN)
  return "clang_plugin_and_fallback_scanner";
#else
  return "fallback_scanner_only";
#endif
}

}  // namespace vmp::bindings::cpp
