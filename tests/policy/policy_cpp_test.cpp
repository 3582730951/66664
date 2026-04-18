#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <vmp/policy/policy_ir.h>

namespace {

using vmp::policy::AnnotationOrigin;
using vmp::policy::IntegrityLevel;
using vmp::policy::JitPolicy;
using vmp::policy::LanguageOrigin;
using vmp::policy::MobileBridgeMode;
using vmp::policy::PlaintextBudget;
using vmp::policy::PlatformCaps;
using vmp::policy::PolicyEntry;
using vmp::policy::PolicyIR;
using vmp::policy::ProtectionDomain;
using vmp::policy::ReactionPolicy;
using vmp::policy::SensitivityLevel;

std::filesystem::path default_fixtures_dir() {
  return std::filesystem::path(VMP_POLICY_FIXTURES_DEFAULT_DIR);
}

std::filesystem::path resolve_fixtures_dir(int argc, char** argv) {
  if (argc >= 2 && argv[1] != nullptr && argv[1][0] != '\0') {
    return std::filesystem::path(argv[1]);
  }
  if (const char* env = std::getenv("VMP_POLICY_FIXTURES_DIR"); env != nullptr && env[0] != '\0') {
    return std::filesystem::path(env);
  }
  return default_fixtures_dir();
}

std::filesystem::path fixture_path(const std::filesystem::path& fixtures_dir, const std::string& name) {
  return fixtures_dir / name;
}

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string read_file(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to read " + path);
  }
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

bool has_error_code(const std::vector<vmp::policy::ValidationError>& errors, const std::string& code) {
  for (const auto& error : errors) {
    if (error.code == code && error.is_error()) {
      return true;
    }
  }
  return false;
}

void test_round_trip(const std::filesystem::path& fixtures_dir) {
  const auto original = vmp::policy::load_from_file(fixture_path(fixtures_dir, "good.json").string());
  const auto text = vmp::policy::save_to_string(original);
  const auto round_trip = vmp::policy::load_from_string(text);
  require(original == round_trip, "round-trip serialization mismatch");
}

void test_combined_annotations() {
  PolicyEntry entry;
  entry.symbol_or_region = "combined";
  entry.language_origin = LanguageOrigin::cpp;
  entry.annotation_origin = AnnotationOrigin::attribute;
  entry.protection_domain = ProtectionDomain::native;
  entry.sensitivity_level = SensitivityLevel::normal;
  entry.plaintext_budget = PlaintextBudget::transient_only;
  vmp::policy::apply_vm_func_annotation(entry);
  vmp::policy::apply_vm_string_annotation(entry);
  require(entry.protection_domain == ProtectionDomain::vm1, "VM_func must force vm1+ execution domain");
  require(entry.sensitivity_level == SensitivityLevel::highly_sensitive,
          "VM_string must force highly_sensitive data handling");
  require(entry.annotation_tags.size() == 2, "combined annotations must preserve both tags");
}

void test_fixture(const std::filesystem::path& file, bool expect_ok, const std::string& expected_error_code = {}) {
  try {
    const auto policy = vmp::policy::load_from_file(file.string());
    const auto errors = vmp::policy::validate(policy);
    const bool has_errors = std::any_of(errors.begin(), errors.end(), [](const auto& error) { return error.is_error(); });
    if (expect_ok) {
      require(!has_errors, "expected policy to validate: " + file.string());
    } else {
      require(has_error_code(errors, expected_error_code),
              "expected validation error code '" + expected_error_code + "' for " + file.string());
    }
  } catch (const std::exception&) {
    if (expect_ok) {
      throw;
    }
    if (!expected_error_code.empty()) {
      return;
    }
  }
}

void test_positive_and_negative_constraints(const std::filesystem::path& fixtures_dir) {
  test_fixture(fixture_path(fixtures_dir, "good.json"), true);
  test_fixture(fixture_path(fixtures_dir, "good_ios_hot_only.json"), true);
  test_fixture(fixture_path(fixtures_dir, "bad_vm_func_native.json"), false, "vm_func_native");
  test_fixture(fixture_path(fixtures_dir, "bad_vm_string_sensitivity.json"), false, "vm_string_sensitivity");
  test_fixture(fixture_path(fixtures_dir, "bad_vm_string_plaintext_budget.json"), false);
  test_fixture(fixture_path(fixtures_dir, "bad_audit_event_type.json"), false, "audit_then_delayed_exit_event_type");
  test_fixture(fixture_path(fixtures_dir, "bad_ios_aggressive.json"), false, "ios_jit_capability");
  test_fixture(fixture_path(fixtures_dir, "bad_vm2_integrity.json"), false, "vm2_integrity_level");
}

void test_schema_contains_expected_fields() {
  const auto schema = vmp::policy::schema_as_json();
  require(schema.find("language_origin") != std::string::npos, "schema missing language_origin");
  require(schema.find("platform_caps") != std::string::npos, "schema missing platform_caps");
  require(schema.find("audit_then_delayed_exit") != std::string::npos,
          "schema missing audit_then_delayed_exit");
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto fixtures_dir = resolve_fixtures_dir(argc, argv);
    test_round_trip(fixtures_dir);
    test_combined_annotations();
    test_positive_and_negative_constraints(fixtures_dir);
    test_schema_contains_expected_fields();
    std::cout << "policy_cpp_test OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "policy_cpp_test failed: " << ex.what() << '\n';
    return 1;
  }
}
