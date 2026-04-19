#include <vmp/runtime/integrity/integrity.h>

#include <algorithm>
#include <cstring>

#include <vmp/runtime/integrity/crc32.h>
#include <vmp/runtime/strings/cipher.h>

namespace vmp::runtime::integrity {
namespace {

bool digest_is_zero(const std::uint8_t digest[32]) noexcept {
  for (std::size_t i = 0; i < 32; ++i) {
    if (digest[i] != 0) {
      return false;
    }
  }
  return true;
}

std::mutex& observer_mutex() {
  static std::mutex mutex;
  return mutex;
}

RegionRegistry::VerificationObserver& observer_slot() {
  static RegionRegistry::VerificationObserver observer;
  return observer;
}

void notify_observer(const RegionVerifyResult& result) {
  RegionRegistry::VerificationObserver observer;
  {
    std::lock_guard<std::mutex> lock(observer_mutex());
    observer = observer_slot();
  }
  if (observer) {
    observer(result);
  }
}

}  // namespace

RegionRegistry& RegionRegistry::instance() noexcept {
  static RegionRegistry registry;
  return registry;
}

void RegionRegistry::set_verification_observer(VerificationObserver observer) {
  std::lock_guard<std::mutex> lock(observer_mutex());
  observer_slot() = std::move(observer);
}

void RegionRegistry::register_region(ProtectedRegion region) {
  if (region.name.empty() || region.base == nullptr || region.size == 0) {
    throw std::runtime_error("integrity: invalid protected region");
  }
  if (!region.expected_crc32.has_value()) {
    region.expected_crc32 = compute_region_crc32(region);
  }
  if (digest_is_zero(region.expected_sha256)) {
    const auto digest = compute_digest(region);
    std::copy(digest.begin(), digest.end(), region.expected_sha256);
  }
  std::lock_guard<std::mutex> lock(mutex_);
  regions_[region.name] = std::move(region);
}

void RegionRegistry::unregister(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  regions_.erase(name);
}

std::vector<ProtectedRegion> RegionRegistry::all() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ProtectedRegion> out;
  out.reserve(regions_.size());
  for (const auto& [_, region] : regions_) {
    out.push_back(region);
  }
  return out;
}

std::vector<RegionVerifyResult> RegionRegistry::verify_all(Mode mode) const {
  const auto snapshot = all();
  std::vector<RegionVerifyResult> out;
  out.reserve(snapshot.size());
  for (const auto& region : snapshot) {
    out.push_back(verify_one(region.name, mode));
  }
  return out;
}

RegionVerifyResult RegionRegistry::verify_one(const std::string& name, Mode mode) const {
  ProtectedRegion region;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = regions_.find(name);
    if (it == regions_.end()) {
      return {name, RegionVerifyStatus::missing, mode, false, false, false};
    }
    region = it->second;
  }
  if (region.base == nullptr || region.size == 0) {
    return {name, RegionVerifyStatus::invalid, mode, false, false, false};
  }

  RegionVerifyResult result;
  result.name = name;
  result.mode = mode;
  result.crc32_match = region.expected_crc32.has_value() ? (compute_region_crc32(region) == *region.expected_crc32) : true;

  if (mode == Mode::fast) {
    result.status = result.crc32_match ? RegionVerifyStatus::ok : RegionVerifyStatus::mismatch;
    if (result.status == RegionVerifyStatus::mismatch) {
      notify_observer(result);
    }
    return result;
  }

  const auto digest = compute_digest(region);
  result.sha256_checked = true;
  result.sha256_match = std::equal(digest.begin(), digest.end(), region.expected_sha256);
  result.status = result.sha256_match ? RegionVerifyStatus::ok : RegionVerifyStatus::mismatch;
  if (result.status == RegionVerifyStatus::mismatch) {
    notify_observer(result);
  }
  return result;
}

RegionVerifyResult RegionRegistry::verify_one_fast(const std::string& name) const { return verify_one(name, Mode::fast); }

void RegionRegistry::reset_for_tests() {
  std::lock_guard<std::mutex> lock(mutex_);
  regions_.clear();
}

std::array<std::uint8_t, 32> RegionRegistry::compute_digest(const ProtectedRegion& region) {
  std::vector<std::uint8_t> material(region.size);
  std::memcpy(material.data(), region.base, region.size);
  const auto digest = vmp::runtime::strings::sha256(material);
  std::array<std::uint8_t, 32> out{};
  std::copy_n(digest.begin(), out.size(), out.begin());
  return out;
}

std::uint32_t RegionRegistry::compute_region_crc32(const ProtectedRegion& region) {
  return crc32_compute(region.base, region.size);
}

const char* to_string(RegionVerifyStatus status) noexcept {
  switch (status) {
    case RegionVerifyStatus::ok: return "ok";
    case RegionVerifyStatus::missing: return "missing";
    case RegionVerifyStatus::invalid: return "invalid";
    case RegionVerifyStatus::mismatch: return "mismatch";
  }
  return "invalid";
}

const char* to_string(RegionRegistry::Mode mode) noexcept {
  switch (mode) {
    case RegionRegistry::Mode::fast: return "fast";
    case RegionRegistry::Mode::authoritative: return "authoritative";
  }
  return "authoritative";
}

const char* Facade::status() const noexcept { return "runtime_integrity_ready"; }

}  // namespace vmp::runtime::integrity
