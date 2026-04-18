#include <string_view>

#include <vmp/bindings/cpp/annotate.h>

namespace sample {

VMP_VM_FUNC int declared_only_cpp(int value);

VMP_VM_FUNC int defined_cpp(int value) {
  return value * 2;
}

VMP_VM_STRING inline constexpr std::string_view kCppSecret = "cpp-secret";
VMP_VM_STRING constexpr char kCppArray[] = "cpp-array";

VMP_VM_FUNC VMP_VM_STRING int dual_cpp(std::string_view input) {
  return static_cast<int>(input.size());
}

int plain_cpp() {
  return 5;
}

}  // namespace sample
