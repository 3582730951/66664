#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace vmp::runtime::state {

struct ProfileEntry {
  std::uint64_t module_id = 0;
  std::uint32_t pc = 0;
  std::uint64_t hits = 0;
  std::uint32_t hot_class = 0;
  double importance = 0.0;

  bool operator==(const ProfileEntry& other) const noexcept;
};

struct OfflineProfile {
  std::uint32_t version = 1;
  std::uint64_t source_seed = 0;
  std::vector<ProfileEntry> entries;
  std::map<std::string, std::string> meta;

  bool operator==(const OfflineProfile& other) const noexcept;
};

struct HotLocationKey {
  std::uint64_t module_id = 0;
  std::uint32_t pc = 0;

  bool operator<(const HotLocationKey& other) const noexcept;
  bool operator==(const HotLocationKey& other) const noexcept;
};

struct HotTraceEdgeKey {
  std::uint64_t module_id = 0;
  std::uint32_t from_pc = 0;
  std::uint32_t to_pc = 0;

  bool operator<(const HotTraceEdgeKey& other) const noexcept;
  bool operator==(const HotTraceEdgeKey& other) const noexcept;
};

struct DomainSwitchKey {
  std::string from;
  std::string to;

  bool operator<(const DomainSwitchKey& other) const noexcept;
  bool operator==(const DomainSwitchKey& other) const noexcept;
};

struct HotRecorderSnapshot {
  std::map<HotLocationKey, std::uint64_t> function_hits;
  std::map<HotLocationKey, std::uint64_t> block_hits;
  std::map<HotTraceEdgeKey, std::uint64_t> trace_edges;
  std::map<HotLocationKey, std::uint64_t> jit_hits;
  std::map<HotLocationKey, std::uint64_t> jit_misses;
  std::map<DomainSwitchKey, std::uint64_t> domain_switches;
  std::map<HotLocationKey, std::uint64_t> sensitive_data_accesses;
  double uptime_seconds = 0.0;
};

OfflineProfile load_from_file(const std::string& path);
void save_to_file(const OfflineProfile& profile, const std::string& path);
std::vector<std::string> validate(const OfflineProfile& profile);
void validate_or_throw(const OfflineProfile& profile);

OfflineProfile fuse_profiles(const OfflineProfile& offline,
                             const HotRecorderSnapshot& online,
                             double online_weight = 0.4);

OfflineProfile merge_profiles(const OfflineProfile& a, const OfflineProfile& b);
std::string diff_profiles(const OfflineProfile& a, const OfflineProfile& b);
OfflineProfile offline_profile_from_snapshot(const HotRecorderSnapshot& snapshot, std::uint64_t source_seed = 0);

double effective_online_weight(const HotRecorderSnapshot& snapshot, double requested_weight = 0.4) noexcept;

}  // namespace vmp::runtime::state
