#include <vmp/policy/policy_ir.h>

#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace vmp::bindings::cpp {
vmp::policy::PolicyIR collect_policy_from_sources_with_fallback(const std::vector<std::string>& sources);
}

int main(int argc, char** argv) {
  try {
    std::string policy_out;
    std::vector<std::string> sources;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg.rfind("--policy-out=", 0) == 0) {
        policy_out = arg.substr(std::string("--policy-out=").size());
      } else if (arg == "--") {
        break;
      } else {
        sources.push_back(arg);
      }
    }
    if (policy_out.empty()) {
      throw std::runtime_error("--policy-out=<path> is required");
    }
    if (sources.empty()) {
      throw std::runtime_error("at least one source path is required");
    }
    const auto policy_ir = vmp::bindings::cpp::collect_policy_from_sources_with_fallback(sources);
    vmp::policy::save_to_file(policy_ir, policy_out);
    std::cout << "fallback collection OK: " << policy_ir.entries.size() << " entries" << std::endl;
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vmp-cpp-fallback-scan: " << ex.what() << std::endl;
    return 1;
  }
}
