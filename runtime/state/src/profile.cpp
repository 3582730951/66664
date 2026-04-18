#include <vmp/runtime/state/profile.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace vmp::runtime::state {
namespace {
using json = nlohmann::json;

void ensure_allowed_keys(const json& object,
                         const std::unordered_set<std::string>& allowed,
                         const std::string& context) {
  if (!object.is_object()) {
    throw std::runtime_error(context + " must be an object");
  }
  for (const auto& [key, _] : object.items()) {
    if (allowed.find(key) == allowed.end()) {
      throw std::runtime_error(context + " contains unknown field '" + key + "'");
    }
  }
}

json to_json(const ProfileEntry& entry) {
  return json{{"module_id", entry.module_id},
              {"pc", entry.pc},
              {"hits", entry.hits},
              {"hot_class", entry.hot_class},
              {"importance", entry.importance}};
}

ProfileEntry from_json_entry(const json& object) {
  static const std::unordered_set<std::string> kAllowed{"module_id", "pc", "hits", "hot_class", "importance"};
  ensure_allowed_keys(object, kAllowed, "entry");
  ProfileEntry entry;
  if (!object.contains("module_id") || !object["module_id"].is_number_unsigned()) {
    throw std::runtime_error("entry.module_id must be an unsigned integer");
  }
  if (!object.contains("pc") || !object["pc"].is_number_unsigned()) {
    throw std::runtime_error("entry.pc must be an unsigned integer");
  }
  if (!object.contains("hits") || !object["hits"].is_number_unsigned()) {
    throw std::runtime_error("entry.hits must be an unsigned integer");
  }
  if (!object.contains("hot_class") || !object["hot_class"].is_number_unsigned()) {
    throw std::runtime_error("entry.hot_class must be an unsigned integer");
  }
  if (!object.contains("importance") || !object["importance"].is_number()) {
    throw std::runtime_error("entry.importance must be numeric");
  }
  entry.module_id = object["module_id"].get<std::uint64_t>();
  entry.pc = object["pc"].get<std::uint32_t>();
  entry.hits = object["hits"].get<std::uint64_t>();
  entry.hot_class = object["hot_class"].get<std::uint32_t>();
  entry.importance = object["importance"].get<double>();
  return entry;
}

std::uint32_t classify_hot(std::uint64_t hits) {
  if (hits >= 1000) return 3;
  if (hits >= 128) return 2;
  if (hits >= 8) return 1;
  return 0;
}

std::string key_string(const HotLocationKey& key) {
  return std::to_string(key.module_id) + ":" + std::to_string(key.pc);
}

}  // namespace

bool ProfileEntry::operator==(const ProfileEntry& other) const noexcept {
  return module_id == other.module_id && pc == other.pc && hits == other.hits && hot_class == other.hot_class &&
         std::abs(importance - other.importance) < 1e-12;
}

bool OfflineProfile::operator==(const OfflineProfile& other) const noexcept {
  return version == other.version && source_seed == other.source_seed && entries == other.entries && meta == other.meta;
}

bool HotLocationKey::operator<(const HotLocationKey& other) const noexcept {
  return module_id < other.module_id || (module_id == other.module_id && pc < other.pc);
}

bool HotLocationKey::operator==(const HotLocationKey& other) const noexcept {
  return module_id == other.module_id && pc == other.pc;
}

bool HotTraceEdgeKey::operator<(const HotTraceEdgeKey& other) const noexcept {
  if (module_id != other.module_id) return module_id < other.module_id;
  if (from_pc != other.from_pc) return from_pc < other.from_pc;
  return to_pc < other.to_pc;
}

bool HotTraceEdgeKey::operator==(const HotTraceEdgeKey& other) const noexcept {
  return module_id == other.module_id && from_pc == other.from_pc && to_pc == other.to_pc;
}

bool DomainSwitchKey::operator<(const DomainSwitchKey& other) const noexcept {
  return from < other.from || (from == other.from && to < other.to);
}

bool DomainSwitchKey::operator==(const DomainSwitchKey& other) const noexcept { return from == other.from && to == other.to; }

OfflineProfile load_from_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to open profile: " + path);
  }
  json root;
  input >> root;
  static const std::unordered_set<std::string> kAllowed{"schema", "version", "source_seed", "entries", "meta"};
  ensure_allowed_keys(root, kAllowed, "profile");
  if (!root.contains("schema") || !root["schema"].is_string() || root["schema"].get<std::string>() != "vp1") {
    throw std::runtime_error("profile.schema must be 'vp1'");
  }
  OfflineProfile profile;
  if (!root.contains("version") || !root["version"].is_number_unsigned()) {
    throw std::runtime_error("profile.version must be an unsigned integer");
  }
  if (!root.contains("source_seed") || !root["source_seed"].is_number_unsigned()) {
    throw std::runtime_error("profile.source_seed must be an unsigned integer");
  }
  if (!root.contains("entries") || !root["entries"].is_array()) {
    throw std::runtime_error("profile.entries must be an array");
  }
  if (!root.contains("meta") || !root["meta"].is_object()) {
    throw std::runtime_error("profile.meta must be an object");
  }
  profile.version = root["version"].get<std::uint32_t>();
  profile.source_seed = root["source_seed"].get<std::uint64_t>();
  for (const auto& item : root["entries"]) {
    profile.entries.push_back(from_json_entry(item));
  }
  for (const auto& [key, value] : root["meta"].items()) {
    if (!value.is_string()) {
      throw std::runtime_error("profile.meta values must be strings");
    }
    profile.meta[key] = value.get<std::string>();
  }
  validate_or_throw(profile);
  return profile;
}

void save_to_file(const OfflineProfile& profile, const std::string& path) {
  validate_or_throw(profile);
  json root;
  root["schema"] = "vp1";
  root["version"] = profile.version;
  root["source_seed"] = profile.source_seed;
  root["entries"] = json::array();
  for (const auto& entry : profile.entries) {
    root["entries"].push_back(to_json(entry));
  }
  root["meta"] = json::object();
  for (const auto& [key, value] : profile.meta) {
    root["meta"][key] = value;
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("failed to write profile: " + path);
  }
  output << root.dump(2) << '\n';
}

std::vector<std::string> validate(const OfflineProfile& profile) {
  std::vector<std::string> errors;
  std::set<std::pair<std::uint64_t, std::uint32_t>> seen;
  for (const auto& entry : profile.entries) {
    if (!(entry.importance >= 0.0 && entry.importance <= 1.0) || std::isnan(entry.importance)) {
      errors.push_back("importance out of range for module=" + std::to_string(entry.module_id) + " pc=" + std::to_string(entry.pc));
    }
    if (entry.hot_class > 3) {
      errors.push_back("hot_class out of range for module=" + std::to_string(entry.module_id) + " pc=" + std::to_string(entry.pc));
    }
    if (!seen.insert({entry.module_id, entry.pc}).second) {
      errors.push_back("duplicate entry for module=" + std::to_string(entry.module_id) + " pc=" + std::to_string(entry.pc));
    }
  }
  return errors;
}

void validate_or_throw(const OfflineProfile& profile) {
  const auto errors = validate(profile);
  if (!errors.empty()) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < errors.size(); ++i) {
      if (i != 0) oss << "; ";
      oss << errors[i];
    }
    throw std::runtime_error(oss.str());
  }
}

double effective_online_weight(const HotRecorderSnapshot& snapshot, double requested_weight) noexcept {
  if (snapshot.uptime_seconds < 60.0) {
    return 0.0;
  }
  if (requested_weight < 0.0) return 0.0;
  if (requested_weight > 1.0) return 1.0;
  return requested_weight;
}

OfflineProfile fuse_profiles(const OfflineProfile& offline, const HotRecorderSnapshot& online, double online_weight) {
  validate_or_throw(offline);
  OfflineProfile fused = offline;
  fused.meta["schema"] = "vp1";
  const auto eff = effective_online_weight(online, online_weight);

  std::map<HotLocationKey, std::uint64_t> online_hits;
  for (const auto& [key, hits] : online.function_hits) online_hits[key] += hits;
  for (const auto& [key, hits] : online.block_hits) online_hits[key] += hits;

  std::uint64_t max_hits = 0;
  for (const auto& [_, hits] : online_hits) max_hits = std::max(max_hits, hits);

  std::map<HotLocationKey, ProfileEntry> merged;
  for (const auto& entry : offline.entries) {
    merged[{entry.module_id, entry.pc}] = entry;
  }

  for (const auto& [key, hits] : online_hits) {
    const double online_importance = max_hits == 0 ? 0.0 : static_cast<double>(hits) / static_cast<double>(max_hits);
    auto it = merged.find(key);
    if (it == merged.end()) {
      ProfileEntry entry;
      entry.module_id = key.module_id;
      entry.pc = key.pc;
      entry.hits = static_cast<std::uint64_t>(std::llround(static_cast<double>(hits) * eff));
      entry.hot_class = classify_hot(hits);
      entry.importance = online_importance * eff;
      merged[key] = entry;
      continue;
    }
    it->second.hits += hits;
    it->second.hot_class = std::max<std::uint32_t>(it->second.hot_class,
                                                   static_cast<std::uint32_t>(std::llround((1.0 - eff) * it->second.hot_class + eff * classify_hot(hits))));
    it->second.hot_class = std::min<std::uint32_t>(3, it->second.hot_class);
    it->second.importance = ((1.0 - eff) * it->second.importance) + (eff * online_importance);
  }

  fused.entries.clear();
  for (const auto& [_, entry] : merged) fused.entries.push_back(entry);
  std::sort(fused.entries.begin(), fused.entries.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.module_id != rhs.module_id) return lhs.module_id < rhs.module_id;
    return lhs.pc < rhs.pc;
  });
  validate_or_throw(fused);
  return fused;
}

OfflineProfile merge_profiles(const OfflineProfile& a, const OfflineProfile& b) {
  validate_or_throw(a);
  validate_or_throw(b);
  OfflineProfile out;
  out.version = std::max(a.version, b.version);
  out.source_seed = a.source_seed ^ b.source_seed;
  out.meta = a.meta;
  out.meta.insert(b.meta.begin(), b.meta.end());
  out.meta["schema"] = "vp1";

  std::map<HotLocationKey, ProfileEntry> merged;
  for (const auto& entry : a.entries) merged[{entry.module_id, entry.pc}] = entry;
  for (const auto& entry : b.entries) {
    auto& slot = merged[{entry.module_id, entry.pc}];
    if (slot.module_id == 0 && slot.pc == 0 && slot.hits == 0 && slot.importance == 0.0 && slot.hot_class == 0) {
      slot = entry;
    } else {
      const auto total_hits = slot.hits + entry.hits;
      const auto weighted_importance = total_hits == 0 ? 0.0 :
          ((slot.importance * static_cast<double>(slot.hits)) + (entry.importance * static_cast<double>(entry.hits))) /
              static_cast<double>(total_hits);
      slot.hits = total_hits;
      slot.hot_class = std::max(slot.hot_class, entry.hot_class);
      slot.importance = weighted_importance;
    }
  }
  for (const auto& [_, entry] : merged) out.entries.push_back(entry);
  std::sort(out.entries.begin(), out.entries.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.module_id != rhs.module_id) return lhs.module_id < rhs.module_id;
    return lhs.pc < rhs.pc;
  });
  validate_or_throw(out);
  return out;
}

std::string diff_profiles(const OfflineProfile& a, const OfflineProfile& b) {
  validate_or_throw(a);
  validate_or_throw(b);
  std::map<HotLocationKey, ProfileEntry> left;
  std::map<HotLocationKey, ProfileEntry> right;
  for (const auto& entry : a.entries) left[{entry.module_id, entry.pc}] = entry;
  for (const auto& entry : b.entries) right[{entry.module_id, entry.pc}] = entry;

  std::size_t added = 0;
  std::size_t removed = 0;
  std::size_t changed = 0;
  for (const auto& [key, entry] : left) {
    auto it = right.find(key);
    if (it == right.end()) {
      ++removed;
    } else if (!(entry == it->second)) {
      ++changed;
    }
  }
  for (const auto& [key, _] : right) {
    if (left.find(key) == left.end()) {
      ++added;
    }
  }

  std::ostringstream oss;
  oss << "profile diff: added=" << added << " removed=" << removed << " entries changed=" << changed;
  return oss.str();
}

OfflineProfile offline_profile_from_snapshot(const HotRecorderSnapshot& snapshot, std::uint64_t source_seed) {
  OfflineProfile profile;
  profile.version = 1;
  profile.source_seed = source_seed;
  profile.meta["schema"] = "vp1";
  profile.meta["uptime_seconds"] = std::to_string(snapshot.uptime_seconds);
  std::map<HotLocationKey, std::uint64_t> hits;
  for (const auto& [key, value] : snapshot.function_hits) hits[key] += value;
  for (const auto& [key, value] : snapshot.block_hits) hits[key] += value;
  std::uint64_t max_hits = 0;
  for (const auto& [_, value] : hits) max_hits = std::max(max_hits, value);
  for (const auto& [key, value] : hits) {
    profile.entries.push_back({key.module_id,
                               key.pc,
                               value,
                               classify_hot(value),
                               max_hits == 0 ? 0.0 : static_cast<double>(value) / static_cast<double>(max_hits)});
  }
  std::sort(profile.entries.begin(), profile.entries.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.module_id != rhs.module_id) return lhs.module_id < rhs.module_id;
    return lhs.pc < rhs.pc;
  });
  validate_or_throw(profile);
  return profile;
}

}  // namespace vmp::runtime::state
