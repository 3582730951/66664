#include "test_common.h"

#include <vmp/runtime/state/profile.h>

#include <iostream>

using namespace vmp::tests::runtime_state;

int main() {
  bool bad_importance = false;
  try {
    vmp::runtime::state::OfflineProfile p;
    p.meta["schema"] = "vp1";
    p.entries.push_back({1, 2, 3, 1, 1.2});
    vmp::runtime::state::validate_or_throw(p);
  } catch (...) {
    bad_importance = true;
  }
  require(bad_importance, "importance > 1 should fail");

  bool bad_hot_class = false;
  try {
    vmp::runtime::state::OfflineProfile p;
    p.meta["schema"] = "vp1";
    p.entries.push_back({1, 2, 3, 9, 0.5});
    vmp::runtime::state::validate_or_throw(p);
  } catch (...) {
    bad_hot_class = true;
  }
  require(bad_hot_class, "hot_class > 3 should fail");

  bool duplicate = false;
  try {
    vmp::runtime::state::OfflineProfile p;
    p.meta["schema"] = "vp1";
    p.entries.push_back({1, 2, 3, 1, 0.5});
    p.entries.push_back({1, 2, 4, 2, 0.4});
    vmp::runtime::state::validate_or_throw(p);
  } catch (...) {
    duplicate = true;
  }
  require(duplicate, "duplicate entries should fail");
  std::cout << "profile_validator_rejects_oob OK\n";
}
