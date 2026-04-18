#include <vmp/bindings/cpp/annotate.h>

VMP_VM_FUNC int vm_declared_only(int value);

VMP_VM_FUNC int vm_defined_c(int value) {
  return value + 1;
}

VMP_VM_STRING static const char c_secret[] = "c-secret";
VMP_VM_STRING static const char c_token[] = "c-token";

VMP_VM_FUNC VMP_VM_STRING int vm_dual_c(const char* input) {
  return input ? input[0] : 0;
}

int plain_c_function(void) {
  return 7;
}
