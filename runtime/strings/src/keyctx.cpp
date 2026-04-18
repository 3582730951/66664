#include <vmp/runtime/strings/keyctx.h>

#include <algorithm>
#include <stdexcept>

#include <vmp/runtime/strings/cipher.h>

namespace vmp::runtime::strings {

std::vector<std::uint8_t> MasterKeyHandle::materialize() const {
  if (!provider_) {
    throw std::runtime_error("strings: master key handle is empty");
  }
  auto value = provider_();
  if (value.size() != 16 && value.size() != 32) {
    secure_memzero(value.data(), value.size());
    throw std::runtime_error("strings: master key must be 16 or 32 bytes");
  }
  return value;
}

DerivedKey::~DerivedKey() { secure_memzero(bytes_.data(), bytes_.size()); }

DerivedKey::DerivedKey(DerivedKey&& other) noexcept { bytes_ = other.bytes_; secure_memzero(other.bytes_.data(), other.bytes_.size()); }

DerivedKey& DerivedKey::operator=(DerivedKey&& other) noexcept {
  if (this != &other) {
    secure_memzero(bytes_.data(), bytes_.size());
    bytes_ = other.bytes_;
    secure_memzero(other.bytes_.data(), other.bytes_.size());
  }
  return *this;
}

KeyContext::KeyContext(MasterKeyHandle master_key_handle, std::vector<std::uint8_t> salt)
    : master_key_handle_(std::move(master_key_handle)), salt_(std::move(salt)) {
  if (!master_key_handle_.valid()) {
    throw std::runtime_error("strings: master key provider required");
  }
  if (salt_.empty()) {
    throw std::runtime_error("strings: salt required");
  }
}

DerivedKey KeyContext::derive_subkey(std::string_view purpose_tag) const {
  auto master = master_key_handle_.materialize();
  const auto prk = hkdf_extract_sha256(salt_, master);
  const auto okm = hkdf_expand_sha256(prk, to_bytes(purpose_tag), 32);
  std::array<std::uint8_t, 32> out{};
  std::copy(okm.begin(), okm.end(), out.begin());
  secure_memzero(master.data(), master.size());
  return DerivedKey(out);
}

}  // namespace vmp::runtime::strings
