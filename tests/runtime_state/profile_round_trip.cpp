#include "test_common.h"

#include <vmp/runtime/state/profile.h>

#include <iostream>

using namespace vmp::tests::runtime_state;

int main() {
  vmp::runtime::state::OfflineProfile profile;
  profile.version = 1;
  profile.source_seed = 0x1234;
  profile.entries.push_back({7, 0x20, 100, 3, 0.75});
  profile.entries.push_back({7, 0x30, 50, 2, 0.25});
  profile.meta["schema"] = "vp1";

  const auto path = temp_path("profile_round_trip", ".json");
  vmp::runtime::state::save_to_file(profile, path.string());
  const auto loaded = vmp::runtime::state::load_from_file(path.string());
  require(loaded == profile, "round trip mismatch");
  std::cout << "profile_round_trip OK\n";
}
