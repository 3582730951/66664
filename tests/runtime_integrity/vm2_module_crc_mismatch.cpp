#include "test_common.h"

#include <iostream>

#include <vmp/runtime/vm2/vm2.h>

using namespace vmp::tests::runtime_integrity;
namespace vm2 = vmp::runtime::vm2;

int main() {
  const auto audit_path = temp_path("vm2_crc_mismatch", ".log");
  ScopedEnvVar audit_path_env("VMP_AUDIT_PATH", audit_path.string());

  const auto module = vm2::assemble_module_text("ildimm r0, 1\nbret\n");
  auto bytes = module.serialize();
  require(!bytes.empty(), "serialized VM2 module must not be empty");
  require(bytes.size() > 24u, "serialized VM2 module must contain protected body bytes");
  bytes[24] ^= 0x22u;

  bool rejected = false;
  try {
    (void)vm2::Vm2Module::load_from_bytes(bytes);
  } catch (const std::exception&) {
    rejected = true;
  }

  require(rejected, "tampered VM2 module must be rejected");
  require(read_all(audit_path).find("vm2_module_crc_mismatch") != std::string::npos, "vm2 CRC mismatch audit missing");
  std::cout << "vm2_module_crc_mismatch OK\n";
  return 0;
}
