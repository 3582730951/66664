#include <iostream>
#include <stdexcept>
#include <string>

#include <vmp/runtime/state/profile.h>

namespace {

int usage(const char* argv0, const std::string& message = {}) {
  if (!message.empty()) {
    std::cerr << "error: " << message << '\n';
  }
  std::cerr << "usage: " << argv0 << " merge <a.json> <b.json> --output <merged.json>\n"
            << "       " << argv0 << " diff <a.json> <b.json>\n"
            << "       " << argv0 << " validate <p.json>\n";
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 3) return usage(argv[0]);
    const std::string cmd = argv[1];
    if (cmd == "merge") {
      if (argc != 6 || std::string(argv[4]) != "--output") {
        return usage(argv[0], "merge requires <a> <b> --output <path>");
      }
      const auto a = vmp::runtime::state::load_from_file(argv[2]);
      const auto b = vmp::runtime::state::load_from_file(argv[3]);
      const auto merged = vmp::runtime::state::merge_profiles(a, b);
      vmp::runtime::state::save_to_file(merged, argv[5]);
      std::cout << "merged entries=" << merged.entries.size() << '\n';
      return 0;
    }
    if (cmd == "diff") {
      if (argc != 4) return usage(argv[0], "diff requires <a> <b>");
      const auto a = vmp::runtime::state::load_from_file(argv[2]);
      const auto b = vmp::runtime::state::load_from_file(argv[3]);
      std::cout << vmp::runtime::state::diff_profiles(a, b) << '\n';
      return 0;
    }
    if (cmd == "validate") {
      if (argc != 3) return usage(argv[0], "validate requires <p>");
      const auto profile = vmp::runtime::state::load_from_file(argv[2]);
      vmp::runtime::state::validate_or_throw(profile);
      std::cout << "profile valid entries=" << profile.entries.size() << '\n';
      return 0;
    }
    return usage(argv[0], "unknown command: " + cmd);
  } catch (const std::exception& ex) {
    std::cerr << "vmp-profile-tool failed: " << ex.what() << '\n';
    return 1;
  }
}
