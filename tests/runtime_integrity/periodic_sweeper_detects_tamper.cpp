#include "test_common.h"

#include <array>
#include <chrono>
#include <iostream>
#include <thread>

#include <vmp/runtime/audit/audit.h>
#include <vmp/runtime/integrity/integrity.h>
#include <vmp/runtime/integrity/periodic_sweeper.h>
#include <vmp/runtime/state/state.h>

using namespace vmp::tests::runtime_integrity;
namespace audit = vmp::runtime::audit;
namespace integrity = vmp::runtime::integrity;
namespace state = vmp::runtime::state;
using namespace std::chrono_literals;

int main() {
  const auto audit_path = temp_path("periodic_sweeper", ".log");
  audit::AuditWriter writer(audit_path);
  auto& runtime = state::RuntimeState::instance();
  runtime.shutdown();
  runtime.init_once(&writer, {"linux", "test", false, 2000});

  std::array<std::uint8_t, 8> bytes{{0,1,2,3,4,5,6,7}};
  auto& registry = integrity::RegionRegistry::instance();
  registry.reset_for_tests();
  integrity::ProtectedRegion region{};
  region.name = "sweeper_region";
  region.base = bytes.data();
  region.size = bytes.size();
  registry.register_region(region);

  integrity::PeriodicSweeper sweeper;
  sweeper.start(100, registry);
  std::this_thread::sleep_for(120ms);
  bytes[6] ^= 0xAAu;

  const auto deadline = std::chrono::steady_clock::now() + 600ms;
  bool degraded = false;
  bool saw_authoritative = false;
  while (std::chrono::steady_clock::now() < deadline) {
    writer.flush();
    const auto log = read_all(audit_path);
    degraded = runtime.current_state() == state::RuntimeStateValue::degraded;
    saw_authoritative = log.find("integrity_authoritative_mismatch") != std::string::npos;
    if (degraded && saw_authoritative) {
      break;
    }
    std::this_thread::sleep_for(25ms);
  }
  sweeper.stop();

  const auto log = read_all(audit_path);
  require(log.find("integrity_fast_mismatch") != std::string::npos, "fast mismatch audit missing");
  require(saw_authoritative, "authoritative mismatch audit missing");
  require(degraded, "periodic sweeper must degrade runtime after confirmed tamper");

  runtime.shutdown();
  std::cout << "periodic_sweeper_detects_tamper OK\n";
  return 0;
}
