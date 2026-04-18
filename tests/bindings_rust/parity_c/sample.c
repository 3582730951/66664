#include <vmp/bindings/cpp/annotate.h>

VMP_VM_FUNC int add(int a, int b) {
  return a + b;
}

VMP_VM_STRING const char GREETING[] = "hello";

const char* uses_literal(void) {
  VMP_VM_STRING static const char LOCAL[] = "secret key";
  return LOCAL;
}
