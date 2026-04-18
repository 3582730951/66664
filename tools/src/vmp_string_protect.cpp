#include <iostream>
#include <string>

#include "string_protect_common.h"

namespace {

struct Options {
  std::string policy;
  std::string out_bin = "string_pool.bin";
  std::string out_idx = "string_pool.idx.json";
  std::string out_kdf = "key_derivation.json";
  std::string master_key_hex;
};

Options parse_args(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--policy") {
      options.policy = argv[++i];
    } else if (arg == "--out-bin") {
      options.out_bin = argv[++i];
    } else if (arg == "--out-idx") {
      options.out_idx = argv[++i];
    } else if (arg == "--out-kdf") {
      options.out_kdf = argv[++i];
    } else if (arg == "--master-key") {
      options.master_key_hex = argv[++i];
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  if (options.policy.empty()) {
    throw std::runtime_error("--policy is required");
  }
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto options = parse_args(argc, argv);
    auto master_key = vmp::tools::strings_tool::resolve_master_key(options.master_key_hex);
    const auto outputs = vmp::tools::strings_tool::protect_policy_strings(options.policy, options.out_bin,
                                                                          options.out_idx, options.out_kdf,
                                                                          master_key);
    vmp::runtime::strings::secure_memzero(master_key.data(), master_key.size());
    std::cout << "strings_protected=" << outputs.protected_count << " bin=" << options.out_bin
              << " idx=" << options.out_idx << " kdf=" << options.out_kdf << '\n';
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vmp-string-protect failed: " << ex.what() << '\n';
    return 1;
  }
}
