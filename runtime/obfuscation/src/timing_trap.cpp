#include <vmp/runtime/obfuscation/timing_trap.h>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>

#include <vmp/runtime/strings/aes256_ctr.h>
#include <vmp/runtime/strings/cipher.h>
#include <vmp/runtime/vm1/vm1.h>
#include <vmp/runtime/vm2/vm2.h>

namespace vmp::runtime::obfuscation {
namespace {

using vmp::runtime::strings::AesCtrNonce;
using vmp::runtime::strings::constant_time_equal;
using vmp::runtime::strings::hkdf_expand_sha256;
using vmp::runtime::strings::hkdf_extract_sha256;
using vmp::runtime::strings::hmac_sha256;
using vmp::runtime::strings::secure_memzero;
using vmp::runtime::strings::sha256;
using vmp::runtime::strings::to_bytes;

constexpr std::array<std::uint8_t, 4> kHeaderMagic{{0x54u, 0x54u, 0x52u, 0x50u}};  // TTRP
constexpr std::array<std::uint8_t, 4> kFooterMagic{{0x54u, 0x54u, 0x45u, 0x4eu}};  // TTEN
constexpr std::uint8_t kTrailerVersion = 1u;
constexpr std::size_t kFixedPrefixSize = 4u + 1u + 1u + 2u + 16u + 4u;
constexpr std::size_t kFixedSuffixSize = 32u + 4u + 4u;

std::mutex& counter_override_mutex() {
  static std::mutex mutex;
  return mutex;
}

std::function<std::uint64_t()>& counter_override_slot() {
  static std::function<std::uint64_t()> provider;
  return provider;
}

std::uint64_t hardware_counter() {
#if defined(__x86_64__) || defined(_M_X64)
#  if defined(_MSC_VER)
  return __rdtsc();
#  else
  unsigned aux = 0;
  return __builtin_ia32_rdtscp(&aux);
#  endif
#elif defined(__aarch64__)
  std::uint64_t value = 0;
  asm volatile("mrs %0, cntvct_el0" : "=r"(value));
  return value;
#else
  return 0;
#endif
}

std::uint64_t read_counter() {
  std::lock_guard<std::mutex> lock(counter_override_mutex());
  if (auto& provider = counter_override_slot(); provider) {
    return provider();
  }
  return hardware_counter();
}

void append_le32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
  }
}

void append_le64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (unsigned shift = 0; shift < 64; shift += 8) {
    out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
  }
}

std::uint32_t read_le32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  if (offset + 4u > bytes.size()) {
    throw std::runtime_error("timing_trap: read_le32 out of range");
  }
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4u; ++i) {
    value |= static_cast<std::uint32_t>(bytes[offset + i]) << (i * 8u);
  }
  return value;
}

std::uint64_t read_le64(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  if (offset + 8u > bytes.size()) {
    throw std::runtime_error("timing_trap: read_le64 out of range");
  }
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < 8u; ++i) {
    value |= static_cast<std::uint64_t>(bytes[offset + i]) << (i * 8u);
  }
  return value;
}

std::array<std::uint8_t, 16> random_nonce() {
  static std::atomic<std::uint64_t> counter{0x71c5f0d2a4b69011ull};
  const auto seed = counter.fetch_add(0x9e3779b97f4a7c15ull, std::memory_order_relaxed) ^ hardware_counter();
  std::vector<std::uint8_t> material;
  material.reserve(32u);
  append_le64(material, seed);
  append_le64(material, seed ^ 0xa53c9e7b1d2f4068ull);
  const auto digest = sha256(material);
  std::array<std::uint8_t, 16> nonce{};
  std::copy_n(digest.begin(), nonce.size(), nonce.begin());
  return nonce;
}

std::vector<std::uint8_t> derive_key_material(const std::array<std::uint8_t, 16>& key_context,
                                              std::string_view info) {
  const std::vector<std::uint8_t> ikm(key_context.begin(), key_context.end());
  const auto prk = hkdf_extract_sha256({}, ikm);
  return hkdf_expand_sha256(prk, to_bytes(info), 32u);
}

std::array<std::uint8_t, 16> digest16(const std::vector<std::uint8_t>& material) {
  const auto digest = sha256(material);
  std::array<std::uint8_t, 16> out{};
  std::copy_n(digest.begin(), out.size(), out.begin());
  return out;
}

std::array<std::uint8_t, 16> vm1_key_context(const vmp::runtime::vm1::Vm1Module& module) {
  std::vector<std::uint8_t> material;
  material.reserve(module.code.size() + module.const_pool.size() * 8u + 32u);
  append_le32(material, module.entry_pc);
  append_le32(material, module.module_flags);
  material.insert(material.end(), module.code.begin(), module.code.end());
  for (const auto byte : module.opcode_map_seed) {
    material.push_back(byte);
  }
  for (const auto& entry : module.const_pool) {
    material.push_back(static_cast<std::uint8_t>(entry.kind));
    append_le32(material, static_cast<std::uint32_t>(entry.bytes.size()));
    material.insert(material.end(), entry.bytes.begin(), entry.bytes.end());
  }
  return digest16(material);
}

std::array<std::uint8_t, 16> vm2_key_context(const vmp::runtime::vm2::Vm2Module& module) {
  if (module.key_context_id != std::array<std::uint8_t, vmp::runtime::vm2::kVm2KeyContextIdSize>{}) {
    return module.key_context_id;
  }
  std::vector<std::uint8_t> material;
  material.reserve(module.code.size() + module.const_pool.size() * 16u + 32u);
  append_le32(material, module.entry_pc);
  append_le32(material, module.module_flags);
  material.insert(material.end(), module.code.begin(), module.code.end());
  for (const auto byte : module.opcode_map_seed) {
    material.push_back(byte);
  }
  for (const auto& entry : module.const_pool) {
    material.insert(material.end(), entry.bytes.begin(), entry.bytes.end());
  }
  return digest16(material);
}

std::vector<std::uint8_t> encode_profile_plaintext(const TimingTrapProfile& profile) {
  std::vector<std::uint8_t> out;
  out.reserve(4u + 4u + 8u * 4u + 4u);
  append_le32(out, profile.checkpoint_interval);
  append_le32(out, profile.consecutive_anomaly_limit);
  append_le64(out, profile.min_cycles);
  append_le64(out, profile.max_cycles);
  append_le64(out, profile.median_cycles);
  append_le64(out, profile.p99_cycles);
  append_le32(out, 0u);
  return out;
}

std::optional<TimingTrapProfile> decode_profile_plaintext(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < 4u + 4u + 8u * 4u + 4u) {
    return std::nullopt;
  }
  TimingTrapProfile out;
  std::size_t offset = 0;
  out.checkpoint_interval = read_le32(bytes, offset);
  offset += 4u;
  out.consecutive_anomaly_limit = read_le32(bytes, offset);
  offset += 4u;
  out.min_cycles = read_le64(bytes, offset);
  offset += 8u;
  out.max_cycles = read_le64(bytes, offset);
  offset += 8u;
  out.median_cycles = read_le64(bytes, offset);
  offset += 8u;
  out.p99_cycles = read_le64(bytes, offset);
  if (out.checkpoint_interval == 0u || out.consecutive_anomaly_limit == 0u || out.min_cycles == 0u ||
      out.p99_cycles == 0u || out.max_cycles < out.min_cycles || out.median_cycles < out.min_cycles ||
      out.median_cycles > out.max_cycles || out.p99_cycles < out.median_cycles) {
    return std::nullopt;
  }
  return out;
}

std::vector<std::uint8_t> encrypt_profile_trailer(const std::array<std::uint8_t, 16>& key_context,
                                                  std::string_view logical_name,
                                                  const TimingTrapProfile& profile) {
  const auto plaintext = encode_profile_plaintext(profile);
  auto enc_key = derive_key_material(key_context, std::string("vmp.metadata.v1|") + std::string(logical_name));
  auto mac_key = derive_key_material(key_context, std::string("vmp.metadata.mac.v1|") + std::string(logical_name));
  const auto nonce = random_nonce();
  const auto ciphertext = vmp::runtime::strings::aes256_ctr_xor(enc_key, nonce, plaintext);

  std::vector<std::uint8_t> mac_input;
  mac_input.insert(mac_input.end(), nonce.begin(), nonce.end());
  mac_input.insert(mac_input.end(), ciphertext.begin(), ciphertext.end());
  const auto tag = hmac_sha256(mac_key, mac_input);

  std::vector<std::uint8_t> out;
  const auto total_size = static_cast<std::uint32_t>(kFixedPrefixSize + ciphertext.size() + 32u + 4u + 4u);
  out.reserve(total_size);
  out.insert(out.end(), kHeaderMagic.begin(), kHeaderMagic.end());
  out.push_back(kTrailerVersion);
  out.push_back(1u);
  out.push_back(0u);
  out.push_back(0u);
  out.insert(out.end(), nonce.begin(), nonce.end());
  append_le32(out, static_cast<std::uint32_t>(ciphertext.size()));
  out.insert(out.end(), ciphertext.begin(), ciphertext.end());
  out.insert(out.end(), tag.begin(), tag.end());
  out.insert(out.end(), kFooterMagic.begin(), kFooterMagic.end());
  append_le32(out, total_size);
  secure_memzero(enc_key.data(), enc_key.size());
  secure_memzero(mac_key.data(), mac_key.size());
  return out;
}

std::optional<TimingTrapProfile> decrypt_profile_trailer(const std::vector<std::uint8_t>& trailer,
                                                         const std::array<std::uint8_t, 16>& key_context,
                                                         std::string_view logical_name) {
  if (trailer.size() < kFixedPrefixSize + kFixedSuffixSize) {
    return std::nullopt;
  }
  if (!std::equal(kHeaderMagic.begin(), kHeaderMagic.end(), trailer.begin())) {
    return std::nullopt;
  }
  if (!std::equal(kFooterMagic.begin(), kFooterMagic.end(), trailer.end() - 8u)) {
    return std::nullopt;
  }
  if (trailer[4] != kTrailerVersion) {
    return std::nullopt;
  }
  const auto total_size = read_le32(trailer, trailer.size() - 4u);
  if (total_size != trailer.size()) {
    return std::nullopt;
  }
  const auto cipher_size = read_le32(trailer, 24u);
  const auto expected_size = kFixedPrefixSize + static_cast<std::size_t>(cipher_size) + 32u + 4u + 4u;
  if (expected_size != trailer.size()) {
    return std::nullopt;
  }

  AesCtrNonce nonce{};
  std::copy_n(trailer.begin() + 8, nonce.size(), nonce.begin());
  const auto cipher_begin = trailer.begin() + static_cast<std::ptrdiff_t>(kFixedPrefixSize);
  const auto cipher_end = cipher_begin + static_cast<std::ptrdiff_t>(cipher_size);
  const std::vector<std::uint8_t> ciphertext(cipher_begin, cipher_end);
  const std::vector<std::uint8_t> observed_tag(cipher_end, cipher_end + 32);

  auto enc_key = derive_key_material(key_context, std::string("vmp.metadata.v1|") + std::string(logical_name));
  auto mac_key = derive_key_material(key_context, std::string("vmp.metadata.mac.v1|") + std::string(logical_name));
  std::vector<std::uint8_t> mac_input;
  mac_input.insert(mac_input.end(), nonce.begin(), nonce.end());
  mac_input.insert(mac_input.end(), ciphertext.begin(), ciphertext.end());
  const auto expected_tag = hmac_sha256(mac_key, mac_input);
  if (!constant_time_equal(expected_tag.data(), observed_tag.data(), observed_tag.size())) {
    secure_memzero(enc_key.data(), enc_key.size());
    secure_memzero(mac_key.data(), mac_key.size());
    return std::nullopt;
  }
  const auto plaintext = vmp::runtime::strings::aes256_ctr_xor(enc_key, nonce, ciphertext);
  secure_memzero(enc_key.data(), enc_key.size());
  secure_memzero(mac_key.data(), mac_key.size());
  return decode_profile_plaintext(plaintext);
}

std::string anomaly_name(TimingTrapAnomaly anomaly) {
  switch (anomaly) {
    case TimingTrapAnomaly::too_fast:
      return "too_fast";
    case TimingTrapAnomaly::too_slow:
      return "too_slow";
    case TimingTrapAnomaly::none:
    default:
      return "none";
  }
}

std::uint64_t mix_u64(std::uint64_t value) noexcept {
  value += 0x9e3779b97f4a7c15ull;
  value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
  value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
  return value ^ (value >> 31u);
}

std::uint64_t derive_bias(std::uint64_t module_id,
                          std::uint32_t checkpoint_pc,
                          std::uint64_t delta_cycles,
                          const TimingTrapProfile& profile,
                          std::uint64_t salt) noexcept {
  const auto mixed = mix_u64(module_id ^ delta_cycles ^ profile.median_cycles ^ profile.p99_cycles ^ salt ^ checkpoint_pc);
  return (mixed & 0x7fu) + 1u;
}

void emit_timing_trap_event(vmp::runtime::audit::ReactionDispatcher* dispatcher,
                            std::string_view domain,
                            std::uint32_t checkpoint_pc,
                            std::uint64_t delta_cycles,
                            const TimingTrapProfile& profile,
                            TimingTrapAnomaly anomaly) noexcept {
  if (dispatcher == nullptr) {
    return;
  }
  std::ostringstream note;
  note << "domain=" << domain
       << " pc=" << checkpoint_pc
       << " delta=" << delta_cycles
       << " min=" << profile.min_cycles
       << " max=" << profile.max_cycles
       << " median=" << profile.median_cycles
       << " p99=" << profile.p99_cycles
       << " anomaly=" << anomaly_name(anomaly);
  const auto record = vmp::runtime::audit::make_event("timing_trap_triggered", note.str(), checkpoint_pc, std::string(domain));
  dispatcher->dispatch_without_exit(record, vmp::runtime::audit::ReactionPolicy::audit_then_delayed_exit);
}

TimingTrapAnomaly classify_anomaly(const TimingTrapProfile& profile, std::uint64_t delta_cycles) noexcept {
  if (profile.p99_cycles != 0u && delta_cycles > profile.p99_cycles * 10u) {
    return TimingTrapAnomaly::too_slow;
  }
  const auto fast_floor = profile.min_cycles / 10u;
  if (fast_floor != 0u && delta_cycles < fast_floor) {
    return TimingTrapAnomaly::too_fast;
  }
  return TimingTrapAnomaly::none;
}

}  // namespace

struct TimingTrapRuntimeState {
  TimingTrapProfile profile{};
  bool enabled = false;
  std::uint64_t dispatch_count = 0;
  bool have_last_counter = false;
  std::uint64_t last_counter = 0;
  std::uint32_t consecutive_anomalies = 0;
  bool siren_active = false;
  bool event_emitted = false;
  std::uint64_t primary_bias = 0;
  std::uint64_t secondary_bias = 0;
};

bool TimingTrapProfile::operator==(const TimingTrapProfile& other) const noexcept {
  return checkpoint_interval == other.checkpoint_interval && min_cycles == other.min_cycles && max_cycles == other.max_cycles &&
         median_cycles == other.median_cycles && p99_cycles == other.p99_cycles &&
         consecutive_anomaly_limit == other.consecutive_anomaly_limit;
}

ScopedTimingTrapCounterOverride::ScopedTimingTrapCounterOverride(std::function<std::uint64_t()> provider) noexcept {
  std::lock_guard<std::mutex> lock(counter_override_mutex());
  previous_ = counter_override_slot();
  counter_override_slot() = std::move(provider);
}

ScopedTimingTrapCounterOverride::~ScopedTimingTrapCounterOverride() {
  std::lock_guard<std::mutex> lock(counter_override_mutex());
  counter_override_slot() = std::move(previous_);
}

void attach_timing_trap_profile(vmp::runtime::vm1::Vm1Module& module, const TimingTrapProfile& profile) {
  module.timing_trap_metadata = encrypt_profile_trailer(vm1_key_context(module), "timing-trap.vm1", profile);
}

void attach_timing_trap_profile(vmp::runtime::vm2::Vm2Module& module, const TimingTrapProfile& profile) {
  module.timing_trap_metadata = encrypt_profile_trailer(vm2_key_context(module), "timing-trap.vm2", profile);
}

std::optional<TimingTrapProfile> decode_timing_trap_profile(const vmp::runtime::vm1::Vm1Module& module) {
  if (module.timing_trap_metadata.empty()) {
    return std::nullopt;
  }
  return decrypt_profile_trailer(module.timing_trap_metadata, vm1_key_context(module), "timing-trap.vm1");
}

std::optional<TimingTrapProfile> decode_timing_trap_profile(const vmp::runtime::vm2::Vm2Module& module) {
  if (module.timing_trap_metadata.empty()) {
    return std::nullopt;
  }
  return decrypt_profile_trailer(module.timing_trap_metadata, vm2_key_context(module), "timing-trap.vm2");
}

std::shared_ptr<TimingTrapRuntimeState> make_timing_trap_state(const vmp::runtime::vm1::Vm1Module& module) {
  auto decoded = decode_timing_trap_profile(module);
  if (!decoded.has_value()) {
    return nullptr;
  }
  auto out = std::make_shared<TimingTrapRuntimeState>();
  out->enabled = true;
  out->profile = *decoded;
  return out;
}

std::shared_ptr<TimingTrapRuntimeState> make_timing_trap_state(const vmp::runtime::vm2::Vm2Module& module) {
  auto decoded = decode_timing_trap_profile(module);
  if (!decoded.has_value()) {
    return nullptr;
  }
  auto out = std::make_shared<TimingTrapRuntimeState>();
  out->enabled = true;
  out->profile = *decoded;
  return out;
}

TimingTrapObservation observe_timing_checkpoint(TimingTrapRuntimeState& state,
                                                std::uint32_t checkpoint_pc,
                                                std::uint64_t module_id,
                                                vmp::runtime::audit::ReactionDispatcher* dispatcher,
                                                std::string_view domain) noexcept {
  TimingTrapObservation out;
  out.checkpoint_pc = checkpoint_pc;
  if (!state.enabled || state.profile.checkpoint_interval == 0u) {
    return out;
  }
  const auto dispatch_count = ++state.dispatch_count;
  if ((dispatch_count % state.profile.checkpoint_interval) == 0u) {
    const auto current = read_counter();
    if (!state.have_last_counter) {
      state.have_last_counter = true;
      state.last_counter = current;
    } else {
      out.delta_cycles = current - state.last_counter;
      state.last_counter = current;
      out.anomaly = classify_anomaly(state.profile, out.delta_cycles);
      if (out.anomaly == TimingTrapAnomaly::none) {
        state.consecutive_anomalies = 0u;
      } else {
        ++state.consecutive_anomalies;
      }
      if (!state.siren_active && out.anomaly != TimingTrapAnomaly::none &&
          state.consecutive_anomalies >= state.profile.consecutive_anomaly_limit) {
        state.siren_active = true;
        state.primary_bias = derive_bias(module_id, checkpoint_pc, out.delta_cycles, state.profile, 0x54494d4554524150ull);
        state.secondary_bias = derive_bias(module_id, checkpoint_pc, out.delta_cycles, state.profile, 0x534952454e425953ull);
        out.triggered_now = true;
        if (!state.event_emitted) {
          emit_timing_trap_event(dispatcher, domain, checkpoint_pc, out.delta_cycles, state.profile, out.anomaly);
          state.event_emitted = true;
        }
      }
    }
  }
  out.siren_active = state.siren_active;
  out.primary_bias = state.primary_bias;
  out.secondary_bias = state.secondary_bias;
  return out;
}

TimingTrapTrailerSplit split_serialized_timing_trap_metadata(const std::vector<std::uint8_t>& bytes) {
  TimingTrapTrailerSplit out;
  out.payload_end = bytes.size();
  if (bytes.size() < 8u) {
    return out;
  }
  const auto footer_magic = bytes.end() - 8u;
  if (!std::equal(kFooterMagic.begin(), kFooterMagic.end(), footer_magic)) {
    return out;
  }
  const auto total_size = read_le32(bytes, bytes.size() - 4u);
  if (total_size < kFixedPrefixSize + kFixedSuffixSize || total_size > bytes.size()) {
    return out;
  }
  const auto start = bytes.size() - total_size;
  if (!std::equal(kHeaderMagic.begin(), kHeaderMagic.end(), bytes.begin() + static_cast<std::ptrdiff_t>(start))) {
    return out;
  }
  out.payload_end = start;
  out.trailer.assign(bytes.begin() + static_cast<std::ptrdiff_t>(start), bytes.end());
  return out;
}

}  // namespace vmp::runtime::obfuscation
