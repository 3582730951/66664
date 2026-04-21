#include <vmp/runtime/self_mod/mutation.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <vmp/runtime/audit/reaction.h>
#include <vmp/runtime/cryptor/rolling_opcode_vm1.h>
#include <vmp/runtime/cryptor/rolling_opcode_vm2.h>
#include <vmp/runtime/integrity/crc32.h>
#include <vmp/runtime/strings/aes256_ctr.h>
#include <vmp/runtime/strings/cipher.h>
#include <vmp/runtime/vm1/vm1.h>
#include <vmp/runtime/vm2/vm2.h>

namespace vmp::runtime::self_mod {
namespace {

using vmp::runtime::audit::AnalysisEventRecord;
using vmp::runtime::audit::AuditWriter;
using vmp::runtime::audit::ReactionDispatcher;
using vmp::runtime::audit::ReactionPolicy;
using vmp::runtime::strings::AesCtrNonce;
using vmp::runtime::strings::aes256_ctr_xor;
using vmp::runtime::strings::constant_time_equal;
using vmp::runtime::strings::hkdf_expand_sha256;
using vmp::runtime::strings::hkdf_extract_sha256;
using vmp::runtime::strings::hmac_sha256;
using vmp::runtime::strings::secure_memzero;
using vmp::runtime::strings::sha256;
using vmp::runtime::strings::to_bytes;

constexpr std::array<std::uint8_t, 4> kHeaderMagic{{'S', 'M', 'O', 'D'}};
constexpr std::array<std::uint8_t, 4> kFooterMagic{{'S', 'M', 'E', 'N'}};
constexpr std::uint8_t kTrailerVersion = 1u;
constexpr std::size_t kFixedPrefixSize = 4u + 1u + 1u + 2u + 16u + 4u;
constexpr std::size_t kFixedSuffixSize = 32u + 4u + 4u;
constexpr std::string_view kMutationInfo = "vmp.selfmod.bytecode.v1";
constexpr std::string_view kMutationMaskInfo = "vmp.selfmod.mask.v1";
constexpr std::string_view kTrailerEncPrefix = "vmp.selfmod.enc.v1|";
constexpr std::string_view kTrailerMacPrefix = "vmp.selfmod.mac.v1|";

struct ActiveMutation {
  MutationRule rule{};
  std::vector<std::uint8_t> mutated_bytes;
};

using StateHashFn = std::function<std::array<std::uint8_t, 32>(std::uint32_t)>;

struct ActiveExecution {
  vmp::runtime::cryptor::VmDomain domain = vmp::runtime::cryptor::VmDomain::vm1;
  std::uint64_t module_id = 0;
  ReactionDispatcher* dispatcher = nullptr;
  StateHashFn state_hash_fn;
  std::optional<ModuleConfig> config;
  std::vector<ActiveMutation> active_mutations;
};

std::mutex& observer_mutex() {
  static std::mutex mutex;
  return mutex;
}

std::vector<ObserverHooks>& observer_stack() {
  static std::vector<ObserverHooks> stack;
  return stack;
}

thread_local std::vector<ActiveExecution> g_execution_stack;

void append_le32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8u) {
    out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
  }
}

void append_le64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (unsigned shift = 0; shift < 64; shift += 8u) {
    out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
  }
}

void append_bytes(std::vector<std::uint8_t>& out, const std::array<std::uint8_t, 32>& bytes) {
  out.insert(out.end(), bytes.begin(), bytes.end());
}

std::uint32_t read_le32(const std::vector<std::uint8_t>& bytes, std::size_t& offset) {
  if (offset + 4u > bytes.size()) {
    throw std::runtime_error("self_mod: read_le32 out of range");
  }
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4u; ++i) {
    value |= static_cast<std::uint32_t>(bytes[offset + i]) << (i * 8u);
  }
  offset += 4u;
  return value;
}

std::array<std::uint8_t, 32> read_array32(const std::vector<std::uint8_t>& bytes, std::size_t& offset) {
  if (offset + 32u > bytes.size()) {
    throw std::runtime_error("self_mod: read_array32 out of range");
  }
  std::array<std::uint8_t, 32> out{};
  std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), out.size(), out.begin());
  offset += out.size();
  return out;
}

std::array<std::uint8_t, 16> digest16(const std::vector<std::uint8_t>& material) {
  const auto digest = sha256(material);
  std::array<std::uint8_t, 16> out{};
  std::copy_n(digest.begin(), out.size(), out.begin());
  return out;
}

std::array<std::uint8_t, 16> vm1_trailer_key_context(const vmp::runtime::vm1::Vm1Module& module) {
  std::vector<std::uint8_t> material;
  material.reserve(64u + module.const_pool.size() * 8u);
  append_le32(material, module.entry_pc);
  append_le32(material, module.module_flags);
  material.insert(material.end(), module.opcode_map_seed.begin(), module.opcode_map_seed.end());
  append_le32(material, static_cast<std::uint32_t>(module.const_pool.size()));
  for (const auto& entry : module.const_pool) {
    material.push_back(static_cast<std::uint8_t>(entry.kind));
    append_le32(material, static_cast<std::uint32_t>(entry.bytes.size()));
  }
  return digest16(material);
}

std::array<std::uint8_t, 16> vm2_trailer_key_context(const vmp::runtime::vm2::Vm2Module& module) {
  if (module.key_context_id != std::array<std::uint8_t, vmp::runtime::vm2::kVm2KeyContextIdSize>{}) {
    return module.key_context_id;
  }
  std::vector<std::uint8_t> material;
  material.reserve(64u + module.const_pool.size() * 16u);
  append_le32(material, module.entry_pc);
  append_le32(material, module.module_flags);
  material.insert(material.end(), module.opcode_map_seed.begin(), module.opcode_map_seed.end());
  append_le32(material, static_cast<std::uint32_t>(module.const_pool.size()));
  return digest16(material);
}

std::vector<std::uint8_t> derive_trailer_key(const std::array<std::uint8_t, 16>& key_context,
                                             std::string_view prefix,
                                             std::string_view logical_name) {
  const std::vector<std::uint8_t> ikm(key_context.begin(), key_context.end());
  const auto prk = hkdf_extract_sha256({}, ikm);
  std::string info(prefix);
  info += logical_name;
  return hkdf_expand_sha256(prk, to_bytes(info), 32u);
}

std::array<std::uint8_t, 32> derive_state_bound_key(const std::array<std::uint8_t, 32>& base_key,
                                                    const std::array<std::uint8_t, 32>& state_hash) {
  const std::vector<std::uint8_t> ikm(base_key.begin(), base_key.end());
  const auto prk = hkdf_extract_sha256({}, ikm);
  const auto okm = hkdf_expand_sha256(prk, std::vector<std::uint8_t>(state_hash.begin(), state_hash.end()), 32u);
  std::array<std::uint8_t, 32> out{};
  std::copy_n(okm.begin(), out.size(), out.begin());
  return out;
}

std::vector<std::uint8_t> derive_mutation_mask(const std::array<std::uint8_t, 32>& derived_key,
                                               std::uint32_t target_pc,
                                               std::uint32_t length) {
  std::vector<std::uint8_t> info = to_bytes(kMutationMaskInfo);
  append_le32(info, target_pc);
  append_le32(info, length);
  return hkdf_expand_sha256(std::vector<std::uint8_t>(derived_key.begin(), derived_key.end()), info, length);
}

AesCtrNonce random_nonce() {
  static std::atomic<std::uint64_t> counter{0x534d4f445f4e4f31ull};
  const auto seed = counter.fetch_add(0x9e3779b97f4a7c15ull, std::memory_order_relaxed);
  std::vector<std::uint8_t> material;
  append_le64(material, seed);
  append_le64(material, seed ^ 0xa5a5a5a5a5a5a5a5ull);
  const auto digest = sha256(material);
  AesCtrNonce nonce{};
  std::copy_n(digest.begin(), nonce.size(), nonce.begin());
  return nonce;
}

std::vector<std::uint8_t> encode_plaintext(const ModuleConfig& config) {
  std::vector<std::uint8_t> out;
  out.reserve(9u + config.mutations.size() * (12u + 32u + 32u + 32u) + config.interlocks.size() * 16u);
  out.push_back(kTrailerVersion);
  append_le32(out, static_cast<std::uint32_t>(config.mutations.size()));
  append_le32(out, static_cast<std::uint32_t>(config.interlocks.size()));
  for (const auto& mutation : config.mutations) {
    append_le32(out, mutation.trigger_pc);
    append_le32(out, mutation.target_pc);
    append_le32(out, mutation.length);
    append_bytes(out, mutation.base_key);
    append_bytes(out, mutation.expected_state_hash);
    append_bytes(out, mutation.expected_hmac);
  }
  for (const auto& interlock : config.interlocks) {
    append_le32(out, interlock.trigger_pc);
    append_le32(out, interlock.protected_pc);
    append_le32(out, interlock.protected_length);
    append_le32(out, interlock.expected_crc32);
  }
  return out;
}

std::optional<ModuleConfig> decode_plaintext(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < 9u) {
    return std::nullopt;
  }
  std::size_t offset = 0;
  const auto version = bytes[offset++];
  if (version != kTrailerVersion) {
    return std::nullopt;
  }
  ModuleConfig config;
  const auto mutation_count = read_le32(bytes, offset);
  const auto interlock_count = read_le32(bytes, offset);
  for (std::uint32_t i = 0; i < mutation_count; ++i) {
    MutationRule rule;
    rule.trigger_pc = read_le32(bytes, offset);
    rule.target_pc = read_le32(bytes, offset);
    rule.length = read_le32(bytes, offset);
    rule.base_key = read_array32(bytes, offset);
    rule.expected_state_hash = read_array32(bytes, offset);
    rule.expected_hmac = read_array32(bytes, offset);
    config.mutations.push_back(rule);
  }
  for (std::uint32_t i = 0; i < interlock_count; ++i) {
    InterlockRule rule;
    rule.trigger_pc = read_le32(bytes, offset);
    rule.protected_pc = read_le32(bytes, offset);
    rule.protected_length = read_le32(bytes, offset);
    rule.expected_crc32 = read_le32(bytes, offset);
    config.interlocks.push_back(rule);
  }
  if (offset != bytes.size()) {
    return std::nullopt;
  }
  return config;
}

std::vector<std::uint8_t> encrypt_trailer(const std::array<std::uint8_t, 16>& key_context,
                                          std::string_view logical_name,
                                          const ModuleConfig& config) {
  auto plaintext = encode_plaintext(config);
  auto enc_key = derive_trailer_key(key_context, kTrailerEncPrefix, logical_name);
  auto mac_key = derive_trailer_key(key_context, kTrailerMacPrefix, logical_name);
  const auto nonce = random_nonce();
  const auto ciphertext = aes256_ctr_xor(enc_key, nonce, plaintext);

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
  secure_memzero(plaintext.data(), plaintext.size());
  secure_memzero(enc_key.data(), enc_key.size());
  secure_memzero(mac_key.data(), mac_key.size());
  return out;
}

std::optional<ModuleConfig> decrypt_trailer(const std::vector<std::uint8_t>& trailer,
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
  std::size_t size_offset = trailer.size() - 4u;
  const auto total_size = read_le32(trailer, size_offset);
  if (total_size != trailer.size()) {
    return std::nullopt;
  }
  std::size_t cursor = 8u;
  AesCtrNonce nonce{};
  std::copy_n(trailer.begin() + static_cast<std::ptrdiff_t>(cursor), nonce.size(), nonce.begin());
  cursor += nonce.size();
  const auto cipher_size = read_le32(trailer, cursor);
  const auto expected_size = kFixedPrefixSize + static_cast<std::size_t>(cipher_size) + 32u + 4u + 4u;
  if (expected_size != trailer.size()) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> ciphertext(trailer.begin() + static_cast<std::ptrdiff_t>(cursor),
                                       trailer.begin() + static_cast<std::ptrdiff_t>(cursor + cipher_size));
  cursor += cipher_size;
  std::vector<std::uint8_t> tag(trailer.begin() + static_cast<std::ptrdiff_t>(cursor),
                                trailer.begin() + static_cast<std::ptrdiff_t>(cursor + 32u));

  auto enc_key = derive_trailer_key(key_context, kTrailerEncPrefix, logical_name);
  auto mac_key = derive_trailer_key(key_context, kTrailerMacPrefix, logical_name);
  std::vector<std::uint8_t> mac_input;
  mac_input.insert(mac_input.end(), nonce.begin(), nonce.end());
  mac_input.insert(mac_input.end(), ciphertext.begin(), ciphertext.end());
  const auto expected_tag = hmac_sha256(mac_key, mac_input);
  if (!constant_time_equal(expected_tag, tag)) {
    secure_memzero(enc_key.data(), enc_key.size());
    secure_memzero(mac_key.data(), mac_key.size());
    secure_memzero(ciphertext.data(), ciphertext.size());
    return std::nullopt;
  }
  auto plaintext = aes256_ctr_xor(enc_key, nonce, ciphertext);
  auto decoded = decode_plaintext(plaintext);
  secure_memzero(enc_key.data(), enc_key.size());
  secure_memzero(mac_key.data(), mac_key.size());
  secure_memzero(ciphertext.data(), ciphertext.size());
  secure_memzero(plaintext.data(), plaintext.size());
  return decoded;
}

std::string module_name(vmp::runtime::cryptor::VmDomain domain) {
  return domain == vmp::runtime::cryptor::VmDomain::vm1 ? "vm1" : "vm2";
}

ObserverHooks current_observers() {
  std::lock_guard<std::mutex> lock(observer_mutex());
  if (observer_stack().empty()) {
    return {};
  }
  return observer_stack().back();
}

void emit_fetch_observation(const FetchObservation& obs) {
  auto hooks = current_observers();
  if (hooks.on_fetch) {
    hooks.on_fetch(obs);
  }
}

void emit_mutation_observation(const MutationObservation& obs) {
  auto hooks = current_observers();
  if (hooks.on_mutation) {
    hooks.on_mutation(obs);
  }
}

std::array<std::uint8_t, 32> to_array32(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() != 32u) {
    throw std::runtime_error("self_mod: expected 32-byte digest");
  }
  std::array<std::uint8_t, 32> out{};
  std::copy_n(bytes.begin(), out.size(), out.begin());
  return out;
}

std::uint64_t bit_cast_u64(double value) {
  std::uint64_t out = 0;
  std::memcpy(&out, &value, sizeof(out));
  return out;
}

std::array<std::uint8_t, 32> compute_vm1_state_hash(const vmp::runtime::vm1::Vm1Context& context,
                                                    std::uint32_t trigger_pc) {
  std::vector<std::uint8_t> material;
  append_le32(material, trigger_pc);
  for (const auto reg : context.vr) {
    append_le64(material, reg);
  }
  for (const auto reg : context.vfr) {
    append_le64(material, bit_cast_u64(reg));
  }
  material.push_back(context.flags.zero ? 1u : 0u);
  material.push_back(context.flags.neg ? 1u : 0u);
  material.push_back(context.flags.carry ? 1u : 0u);
  material.push_back(context.flags.overflow ? 1u : 0u);
  const auto epoch_id = context.module == nullptr ? 0u : vmp::runtime::cryptor::vm1::current_epoch_id(*context.module);
  append_le32(material, epoch_id);
  return to_array32(sha256(material));
}

std::array<std::uint8_t, 32> compute_vm2_state_hash(const vmp::runtime::vm2::Vm2Context& context,
                                                    std::uint32_t trigger_pc) {
  std::vector<std::uint8_t> material;
  append_le32(material, trigger_pc);
  for (const auto reg : context.r) {
    append_le64(material, reg);
  }
  for (const auto& vec : context.q) {
    append_le64(material, vec.u64.lo);
    append_le64(material, vec.u64.hi);
  }
  for (const auto reg : context.d) {
    append_le64(material, bit_cast_u64(reg));
  }
  for (const auto pred : context.p) {
    material.push_back(pred ? 1u : 0u);
  }
  const auto epoch_id = context.module == nullptr ? 0u : vmp::runtime::cryptor::vm2::current_epoch_id(*context.module);
  append_le32(material, epoch_id);
  return to_array32(sha256(material));
}

std::vector<std::uint8_t> read_window(const ByteProvider& provider,
                                      std::uint32_t target_pc,
                                      std::uint32_t length) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(length);
  for (std::uint32_t i = 0; i < length; ++i) {
    bytes.push_back(provider(static_cast<std::size_t>(target_pc + i)));
  }
  return bytes;
}

ActiveExecution* current_execution(vmp::runtime::cryptor::VmDomain domain, std::uint64_t module_id) {
  for (auto it = g_execution_stack.rbegin(); it != g_execution_stack.rend(); ++it) {
    if (it->domain == domain && it->module_id == module_id) {
      return &*it;
    }
  }
  return nullptr;
}

AnalysisEventRecord make_self_mod_event(vmp::runtime::cryptor::VmDomain domain,
                                        std::string event_type,
                                        std::string note,
                                        std::uint32_t pc) {
  return vmp::runtime::audit::make_event(std::move(event_type),
                                         std::move(note),
                                         pc,
                                         module_name(domain),
                                         {},
                                         0);
}

void dispatch_event(ActiveExecution& execution,
                    const std::string& event_type,
                    const std::string& note,
                    std::uint32_t pc,
                    ReactionPolicy policy) {
  auto record = make_self_mod_event(execution.domain, event_type, note, pc);
  if (execution.dispatcher != nullptr) {
    execution.dispatcher->dispatch(record, policy);
    return;
  }
  try {
    AuditWriter writer(AuditWriter::default_path());
    ReactionDispatcher dispatcher(writer, policy);
    dispatcher.set_exit_fn([] {});
    dispatcher.dispatch(record, policy);
  } catch (...) {
  }
}

std::string mutation_note(const MutationRule& rule) {
  std::ostringstream oss;
  oss << "trigger_pc=" << rule.trigger_pc << " target_pc=" << rule.target_pc << " length=" << rule.length;
  return oss.str();
}

std::string interlock_note(const InterlockRule& rule, std::uint32_t actual_crc32) {
  std::ostringstream oss;
  oss << "trigger_pc=" << rule.trigger_pc
      << " protected_pc=" << rule.protected_pc
      << " protected_length=" << rule.protected_length
      << " expected_crc32=" << rule.expected_crc32
      << " actual_crc32=" << actual_crc32;
  return oss.str();
}

void maybe_check_interlocks(ActiveExecution& execution,
                            std::uint32_t forward_pc,
                            const ByteProvider& base_provider) {
  if (!execution.config.has_value()) {
    return;
  }
  for (const auto& interlock : execution.config->interlocks) {
    if (interlock.trigger_pc != forward_pc) {
      continue;
    }
    const auto bytes = read_window(base_provider, interlock.protected_pc, interlock.protected_length);
    const auto actual_crc32 = vmp::runtime::integrity::crc32_compute(bytes.data(), bytes.size());
    if (actual_crc32 != interlock.expected_crc32) {
      dispatch_event(execution,
                     "interlock_checksum_mismatch",
                     interlock_note(interlock, actual_crc32),
                     forward_pc,
                     ReactionPolicy::audit_then_delayed_exit);
    }
  }
}

void erase_existing_mutation(ActiveExecution& execution, const MutationRule& rule) {
  execution.active_mutations.erase(
      std::remove_if(execution.active_mutations.begin(),
                     execution.active_mutations.end(),
                     [&](const ActiveMutation& active) {
                       return active.rule.trigger_pc == rule.trigger_pc &&
                              active.rule.target_pc == rule.target_pc &&
                              active.rule.length == rule.length;
                     }),
      execution.active_mutations.end());
}

void maybe_apply_mutations(ActiveExecution& execution,
                           std::uint32_t forward_pc,
                           const ByteProvider& base_provider) {
  if (!execution.config.has_value()) {
    return;
  }
  for (const auto& mutation : execution.config->mutations) {
    if (mutation.trigger_pc != forward_pc) {
      continue;
    }
    const auto state_hash = execution.state_hash_fn ? execution.state_hash_fn(forward_pc)
                                                    : std::array<std::uint8_t, 32>{};
    const auto derived_key = derive_state_bound_key(mutation.base_key, state_hash);
    const auto original_bytes = read_window(base_provider, mutation.target_pc, mutation.length);
    const auto computed_hmac = hmac_sha256(
        std::vector<std::uint8_t>(derived_key.begin(), derived_key.end()),
        original_bytes);
    const auto state_hash_matches = constant_time_equal(state_hash.data(),
                                                        mutation.expected_state_hash.data(),
                                                        mutation.expected_state_hash.size());
    const auto hmac_matches = constant_time_equal(computed_hmac.data(),
                                                  mutation.expected_hmac.data(),
                                                  mutation.expected_hmac.size());
    if (!state_hash_matches || !hmac_matches) {
      dispatch_event(execution,
                     "bytecode_hmac_divergence",
                     mutation_note(mutation),
                     forward_pc,
                     ReactionPolicy::audit_then_delayed_exit);
      continue;
    }
    auto mask = derive_mutation_mask(derived_key, mutation.target_pc, mutation.length);
    std::vector<std::uint8_t> mutated_bytes(original_bytes.size(), 0u);
    for (std::size_t i = 0; i < original_bytes.size(); ++i) {
      mutated_bytes[i] = static_cast<std::uint8_t>(original_bytes[i] ^ mask[i]);
    }
    erase_existing_mutation(execution, mutation);
    execution.active_mutations.push_back(ActiveMutation{mutation, mutated_bytes});
    dispatch_event(execution,
                   "bytecode_self_modified",
                   mutation_note(mutation),
                   forward_pc,
                   ReactionPolicy::audit_only);
    emit_mutation_observation(MutationObservation{
        execution.domain,
        execution.module_id,
        mutation.trigger_pc,
        mutation.target_pc,
        original_bytes,
        mutated_bytes,
    });
  }
}

std::uint8_t mutated_byte_for_pc(const ActiveExecution& execution,
                                 std::uint32_t forward_pc,
                                 std::uint8_t static_byte,
                                 bool& mutated) {
  for (auto it = execution.active_mutations.rbegin(); it != execution.active_mutations.rend(); ++it) {
    const auto begin = it->rule.target_pc;
    const auto end = it->rule.target_pc + it->rule.length;
    if (forward_pc >= begin && forward_pc < end) {
      mutated = true;
      return it->mutated_bytes[forward_pc - begin];
    }
  }
  mutated = false;
  return static_byte;
}

void prune_finished_mutations(ActiveExecution& execution, std::uint32_t forward_pc) {
  execution.active_mutations.erase(
      std::remove_if(execution.active_mutations.begin(),
                     execution.active_mutations.end(),
                     [&](const ActiveMutation& active) {
                       const auto end = active.rule.target_pc + active.rule.length;
                       return active.rule.length != 0u && forward_pc >= active.rule.target_pc && forward_pc + 1u >= end;
                     }),
      execution.active_mutations.end());
}

std::uint8_t fetch_byte_with_policy(vmp::runtime::cryptor::VmDomain domain,
                                    std::uint64_t module_id,
                                    std::uint32_t forward_pc,
                                    const ByteProvider& base_provider) {
  const auto static_byte = base_provider(forward_pc);
  auto* execution = current_execution(domain, module_id);
  if (execution == nullptr || !execution->config.has_value()) {
    emit_fetch_observation(FetchObservation{domain, module_id, forward_pc, static_byte, static_byte, false});
    return static_byte;
  }
  maybe_check_interlocks(*execution, forward_pc, base_provider);
  maybe_apply_mutations(*execution, forward_pc, base_provider);
  bool mutated = false;
  const auto runtime_byte = mutated_byte_for_pc(*execution, forward_pc, static_byte, mutated);
  emit_fetch_observation(FetchObservation{domain, module_id, forward_pc, static_byte, runtime_byte, mutated});
  if (mutated) {
    prune_finished_mutations(*execution, forward_pc);
  }
  return runtime_byte;
}

void push_execution(ActiveExecution execution) {
  g_execution_stack.push_back(std::move(execution));
}

void pop_execution() {
  if (!g_execution_stack.empty()) {
    g_execution_stack.pop_back();
  }
}

ModuleConfig decode_or_empty(const vmp::runtime::vm1::Vm1Module& module) {
  const auto decoded = decode(module);
  return decoded.value_or(ModuleConfig{});
}

ModuleConfig decode_or_empty(const vmp::runtime::vm2::Vm2Module& module) {
  const auto decoded = decode(module);
  return decoded.value_or(ModuleConfig{});
}

}  // namespace

ScopedObserverHooks::ScopedObserverHooks(ObserverHooks hooks) {
  std::lock_guard<std::mutex> lock(observer_mutex());
  observer_stack().push_back(std::move(hooks));
  active_ = true;
}

ScopedObserverHooks::~ScopedObserverHooks() {
  if (!active_) {
    return;
  }
  std::lock_guard<std::mutex> lock(observer_mutex());
  if (!observer_stack().empty()) {
    observer_stack().pop_back();
  }
}

ScopedExecution::ScopedExecution(vmp::runtime::vm1::Vm1Context& context) {
  ActiveExecution execution;
  execution.domain = vmp::runtime::cryptor::VmDomain::vm1;
  execution.module_id = context.module == nullptr ? 0u : context.module->id();
  execution.dispatcher = context.audit_dispatcher;
  execution.state_hash_fn = [&context](std::uint32_t trigger_pc) { return compute_vm1_state_hash(context, trigger_pc); };
  execution.config = decode_or_empty(*context.module);
  push_execution(std::move(execution));
  active_ = true;
}

ScopedExecution::ScopedExecution(vmp::runtime::vm2::Vm2Context& context) {
  ActiveExecution execution;
  execution.domain = vmp::runtime::cryptor::VmDomain::vm2;
  execution.module_id = context.module == nullptr ? 0u : context.module->id();
  execution.dispatcher = context.audit_dispatcher;
  execution.state_hash_fn = [&context](std::uint32_t trigger_pc) { return compute_vm2_state_hash(context, trigger_pc); };
  execution.config = decode_or_empty(*context.module);
  push_execution(std::move(execution));
  active_ = true;
}

ScopedExecution::~ScopedExecution() {
  if (active_) {
    pop_execution();
  }
}

MutationRule make_vm1_mutation_rule(const vmp::runtime::vm1::Vm1Context& context,
                                    std::uint32_t trigger_pc,
                                    std::uint32_t target_pc,
                                    std::uint32_t length,
                                    const std::array<std::uint8_t, 32>& base_key) {
  if (context.module == nullptr) {
    throw std::runtime_error("self_mod: vm1 context has no module");
  }
  if (length == 0u || static_cast<std::size_t>(target_pc) + static_cast<std::size_t>(length) > context.module->code.size()) {
    throw std::runtime_error("self_mod: vm1 mutation range out of bounds");
  }
  MutationRule rule;
  rule.trigger_pc = trigger_pc;
  rule.target_pc = target_pc;
  rule.length = length;
  rule.base_key = base_key;
  rule.expected_state_hash = compute_vm1_state_hash(context, trigger_pc);
  const auto derived_key = derive_state_bound_key(rule.base_key, rule.expected_state_hash);
  const std::vector<std::uint8_t> original_bytes(context.module->code.begin() + static_cast<std::ptrdiff_t>(target_pc),
                                                 context.module->code.begin() + static_cast<std::ptrdiff_t>(target_pc + length));
  const auto digest = hmac_sha256(std::vector<std::uint8_t>(derived_key.begin(), derived_key.end()), original_bytes);
  std::copy_n(digest.begin(), rule.expected_hmac.size(), rule.expected_hmac.begin());
  return rule;
}

MutationRule make_vm2_mutation_rule(const vmp::runtime::vm2::Vm2Context& context,
                                    std::uint32_t trigger_pc,
                                    std::uint32_t target_pc,
                                    std::uint32_t length,
                                    const std::array<std::uint8_t, 32>& base_key) {
  if (context.module == nullptr) {
    throw std::runtime_error("self_mod: vm2 context has no module");
  }
  if (length == 0u || static_cast<std::size_t>(target_pc) + static_cast<std::size_t>(length) > context.module->code.size()) {
    throw std::runtime_error("self_mod: vm2 mutation range out of bounds");
  }
  MutationRule rule;
  rule.trigger_pc = trigger_pc;
  rule.target_pc = target_pc;
  rule.length = length;
  rule.base_key = base_key;
  rule.expected_state_hash = compute_vm2_state_hash(context, trigger_pc);
  const auto derived_key = derive_state_bound_key(rule.base_key, rule.expected_state_hash);
  const std::vector<std::uint8_t> original_bytes(context.module->code.begin() + static_cast<std::ptrdiff_t>(target_pc),
                                                 context.module->code.begin() + static_cast<std::ptrdiff_t>(target_pc + length));
  const auto digest = hmac_sha256(std::vector<std::uint8_t>(derived_key.begin(), derived_key.end()), original_bytes);
  std::copy_n(digest.begin(), rule.expected_hmac.size(), rule.expected_hmac.begin());
  return rule;
}

void attach(vmp::runtime::vm1::Vm1Module& module, const ModuleConfig& config) {
  module.self_mod_metadata = encrypt_trailer(vm1_trailer_key_context(module), "self-mod.vm1", config);
}

void attach(vmp::runtime::vm2::Vm2Module& module, const ModuleConfig& config) {
  module.self_mod_metadata = encrypt_trailer(vm2_trailer_key_context(module), "self-mod.vm2", config);
}

std::optional<ModuleConfig> decode(const vmp::runtime::vm1::Vm1Module& module) {
  if (module.self_mod_metadata.empty()) {
    return std::nullopt;
  }
  return decrypt_trailer(module.self_mod_metadata, vm1_trailer_key_context(module), "self-mod.vm1");
}

std::optional<ModuleConfig> decode(const vmp::runtime::vm2::Vm2Module& module) {
  if (module.self_mod_metadata.empty()) {
    return std::nullopt;
  }
  return decrypt_trailer(module.self_mod_metadata, vm2_trailer_key_context(module), "self-mod.vm2");
}

TrailerSplit split_serialized_metadata(const std::vector<std::uint8_t>& bytes) {
  TrailerSplit out;
  out.payload_end = bytes.size();
  if (bytes.size() < 8u) {
    return out;
  }
  const auto footer_magic = bytes.end() - 8u;
  if (!std::equal(kFooterMagic.begin(), kFooterMagic.end(), footer_magic)) {
    return out;
  }
  std::size_t size_offset = bytes.size() - 4u;
  const auto total_size = read_le32(bytes, size_offset);
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

std::uint8_t fetch_vm1_byte(const vmp::runtime::vm1::Vm1Module& module,
                            std::size_t forward_pc,
                            const ByteProvider& base_provider) {
  return fetch_byte_with_policy(vmp::runtime::cryptor::VmDomain::vm1,
                                module.id(),
                                static_cast<std::uint32_t>(forward_pc),
                                base_provider);
}

std::uint8_t fetch_vm2_byte(const vmp::runtime::vm2::Vm2Module& module,
                            std::size_t forward_pc,
                            const ByteProvider& base_provider) {
  return fetch_byte_with_policy(vmp::runtime::cryptor::VmDomain::vm2,
                                module.id(),
                                static_cast<std::uint32_t>(forward_pc),
                                base_provider);
}

}  // namespace vmp::runtime::self_mod
