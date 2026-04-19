#include "test_common.h"

#include <iostream>

#include <vmp/runtime/vm1/vm1.h>

using namespace vmp::tests::runtime_integrity;
namespace vm1 = vmp::runtime::vm1;

int main() {
  const auto audit_path = temp_path("vm1_crc_mismatch", ".log");
  ScopedEnvVar audit_path_env("VMP_AUDIT_PATH", audit_path.string());

  const auto module = vm1::assemble_module_text("ldi_u64 vr0, 1\nret\n");
  auto bytes = module.serialize();
  require(!bytes.empty(), "serialized VM1 module must not be empty");
  bytes.back() ^= 0x11u;

  bool rejected = false;
  try {
    (void)vm1::Vm1Module::load_from_bytes(bytes);
  } catch (const std::exception&) {
    rejected = true;
  }

  require(rejected, "tampered VM1 module must be rejected");
  require(read_all(audit_path).find("vm1_module_crc_mismatch") != std::string::npos, "vm1 CRC mismatch audit missing");
  std::cout << "vm1_module_crc_mismatch OK\n";
  return 0;
}
