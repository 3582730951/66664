#include <cstdlib>
#include <filesystem>
#include <iostream>

#include <vmp/policy/policy_ir.h>

namespace {

std::filesystem::path default_fixtures_dir() {
  return std::filesystem::path(VMP_POLICY_FIXTURES_DEFAULT_DIR);
}

std::filesystem::path resolve_fixtures_dir() {
  if (const char* env = std::getenv("VMP_POLICY_FIXTURES_DIR"); env != nullptr && env[0] != '\0') {
    return std::filesystem::path(env);
  }
  return default_fixtures_dir();
}

std::filesystem::path resolve_policy_path(int argc, char** argv) {
  if (argc >= 2 && argv[1] != nullptr && argv[1][0] != '\0') {
    const std::filesystem::path candidate(argv[1]);
    if (candidate.is_absolute() || candidate.has_parent_path()) {
      return candidate;
    }
    return resolve_fixtures_dir() / candidate;
  }
  return resolve_fixtures_dir() / "good.json";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto policy = vmp::policy::load_from_file(resolve_policy_path(argc, argv).string());
    const auto& first = policy.entries.at(0);
    std::cout << "{\"schema_version\":" << policy.schema_version << ",\"entry_count\":" << policy.entries.size()
              << ",\"first\":{\"symbol_or_region\":\"" << first.symbol_or_region
              << "\",\"language_origin\":\"" << vmp::policy::to_string(first.language_origin)
              << "\",\"protection_domain\":\"" << vmp::policy::to_string(first.protection_domain)
              << "\",\"sensitivity_level\":\"" << vmp::policy::to_string(first.sensitivity_level)
              << "\",\"annotation_tags\":[";
    for (std::size_t i = 0; i < first.annotation_tags.size(); ++i) {
      if (i != 0) {
        std::cout << ',';
      }
      std::cout << '"' << first.annotation_tags[i] << '"';
    }
    std::cout << "]}}\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }
}
