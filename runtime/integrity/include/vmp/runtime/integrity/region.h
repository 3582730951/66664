#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vmp::runtime::integrity {

struct ProtectedRegion {
  std::string name;
  const void* base = nullptr;
  std::size_t size = 0;
  std::uint8_t expected_sha256[32]{};
  std::optional<std::uint32_t> expected_crc32;
  std::uint32_t flags = 0;
};

enum class RegionVerifyStatus {
  ok,
  missing,
  invalid,
  mismatch,
};

struct RegionVerifyResult;

class RegionRegistry {
 public:
  enum class Mode {
    fast,
    authoritative,
  };

  using VerificationObserver = std::function<void(const RegionVerifyResult&)>;

  static RegionRegistry& instance() noexcept;
  static void set_verification_observer(VerificationObserver observer);

  RegionRegistry(const RegionRegistry&) = delete;
  RegionRegistry& operator=(const RegionRegistry&) = delete;

  void register_region(ProtectedRegion region);
  void unregister(const std::string& name);
  std::vector<ProtectedRegion> all() const;
  std::vector<RegionVerifyResult> verify_all(Mode mode = Mode::authoritative) const;
  RegionVerifyResult verify_one(const std::string& name, Mode mode = Mode::authoritative) const;
  RegionVerifyResult verify_one_fast(const std::string& name) const;
  void reset_for_tests();

 private:
  RegionRegistry() = default;

  static std::array<std::uint8_t, 32> compute_digest(const ProtectedRegion& region);
  static std::uint32_t compute_region_crc32(const ProtectedRegion& region);

  mutable std::mutex mutex_;
  std::unordered_map<std::string, ProtectedRegion> regions_;
};

struct RegionVerifyResult {
  std::string name;
  RegionVerifyStatus status = RegionVerifyStatus::invalid;
  RegionRegistry::Mode mode = RegionRegistry::Mode::authoritative;
  bool crc32_match = false;
  bool sha256_checked = false;
  bool sha256_match = false;
};

const char* to_string(RegionVerifyStatus status) noexcept;
const char* to_string(RegionRegistry::Mode mode) noexcept;

}  // namespace vmp::runtime::integrity
