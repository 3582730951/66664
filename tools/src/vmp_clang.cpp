#include "vmp_driver_common.h"

#ifndef VMP_CPP_FALLBACK_SCANNER_PATH
#define VMP_CPP_FALLBACK_SCANNER_PATH "vmp-cpp-fallback-scan"
#endif

#ifndef VMP_CPP_PLUGIN_PATH
#define VMP_CPP_PLUGIN_PATH ""
#endif

#ifndef VMP_CPP_COLLECTOR_TOOL_PATH
#define VMP_CPP_COLLECTOR_TOOL_PATH ""
#endif

int main(int argc, char** argv) {
  return vmp::tools::run_vmp_driver({"clang", VMP_CPP_FALLBACK_SCANNER_PATH, VMP_CPP_COLLECTOR_TOOL_PATH, VMP_CPP_PLUGIN_PATH}, argc, argv);
}
