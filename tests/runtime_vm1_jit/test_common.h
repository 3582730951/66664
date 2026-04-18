#pragma once

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#include <vmp/runtime/audit/audit.h>
#include <vmp/runtime/audit/reaction.h>
#include <vmp/runtime/bridge/bridge.h>
#include <vmp/runtime/jit/vm1_jit.h>
#include <vmp/runtime/state/state.h>
#include <vmp/runtime/strings/cipher.h>
#include <vmp/runtime/strings/keyctx.h>
#include <vmp/runtime/vm1/vm1.h>

namespace vmp::tests::runtime_vm1_jit {

inline void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct EnvGuard {
  std::string name;
  std::string original;
  bool had_original = false;

  EnvGuard(std::string name_in, std::string value) : name(std::move(name_in)) {
    if (const char* current = std::getenv(name.c_str()); current != nullptr) {
      had_original = true;
      original = current;
    }
#if defined(_WIN32)
    _putenv_s(name.c_str(), value.c_str());
#else
    ::setenv(name.c_str(), value.c_str(), 1);
#endif
  }

  ~EnvGuard() {
#if defined(_WIN32)
    _putenv_s(name.c_str(), had_original ? original.c_str() : "");
#else
    if (had_original) {
      ::setenv(name.c_str(), original.c_str(), 1);
    } else {
      ::unsetenv(name.c_str());
    }
#endif
  }
};

inline std::filesystem::path temp_path(const std::string& stem, const std::string& ext) {
  return std::filesystem::temp_directory_path() /
         (stem + "_" + std::to_string(::getpid()) + "_" + std::to_string(std::rand()) + ext);
}

inline std::vector<std::uint8_t> fixed_master_key_bytes() {
  std::vector<std::uint8_t> key(32);
  for (std::size_t i = 0; i < key.size(); ++i) {
    key[i] = static_cast<std::uint8_t>(0x41 + i);
  }
  return key;
}

inline vmp::runtime::strings::KeyContext fixed_key_context(std::vector<std::uint8_t> salt = std::vector<std::uint8_t>(16, 0x5A)) {
  return vmp::runtime::strings::KeyContext(
      vmp::runtime::strings::MasterKeyHandle([] { return fixed_master_key_bytes(); }), std::move(salt));
}

inline std::shared_ptr<vmp::runtime::strings::StringPool> make_string_pool(
    const std::vector<std::pair<std::uint32_t, std::string>>& items,
    std::vector<std::uint8_t>* salt_out = nullptr) {
  auto salt = std::vector<std::uint8_t>(16, 0x5A);
  auto key = fixed_key_context(salt);
  const auto subkey = key.derive_subkey("string-pool");
  std::vector<std::uint8_t> ciphertext;
  vmp::runtime::strings::IndexMap index;
  for (const auto& [id, text] : items) {
    const auto nonce = vmp::runtime::strings::u32_to_nonce(id + 1);
    const auto record = vmp::runtime::strings::encrypt_string_record(subkey.bytes(), nonce,
                                                                     vmp::runtime::strings::to_bytes(text));
    vmp::runtime::strings::StringIndexEntry entry;
    entry.offset = static_cast<std::uint32_t>(ciphertext.size());
    entry.length = static_cast<std::uint32_t>(record.ciphertext.size());
    entry.nonce = nonce;
    ciphertext.insert(ciphertext.end(), record.ciphertext.begin(), record.ciphertext.end());
    index[id] = entry;
  }
  if (salt_out != nullptr) {
    *salt_out = salt;
  }
  return std::make_shared<vmp::runtime::strings::StringPool>(std::move(ciphertext), std::move(index), fixed_key_context(salt));
}

inline std::string read_all(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::ostringstream oss;
  oss << input.rdbuf();
  return oss.str();
}

inline std::uint64_t run_module(vmp::runtime::vm1::Vm1Module& module,
                                const std::string& backend,
                                vmp::runtime::bridge::BridgeRegistry* bridge = nullptr,
                                vmp::runtime::audit::ReactionDispatcher* dispatcher = nullptr,
                                std::shared_ptr<vmp::runtime::strings::StringPool> pool = nullptr) {
  EnvGuard backend_guard("VMP_JIT_BACKEND", backend);
  vmp::runtime::jit::Vm1Jit::instance().reset_for_tests();
  vmp::runtime::vm1::Vm1Context context(module);
  context.bridge_registry = bridge;
  context.audit_dispatcher = dispatcher;
  context.string_pool = std::move(pool);
  vmp::runtime::vm1::Vm1Interpreter interpreter;
  const auto result = interpreter.execute(context);
  return result.ret_int;
}

inline std::uint64_t run_module_reuse(vmp::runtime::vm1::Vm1Module& module,
                                      const std::string& backend,
                                      int runs,
                                      vmp::runtime::bridge::BridgeRegistry* bridge = nullptr,
                                      vmp::runtime::audit::ReactionDispatcher* dispatcher = nullptr) {
  EnvGuard backend_guard("VMP_JIT_BACKEND", backend);
  vmp::runtime::jit::Vm1Jit::instance().reset_for_tests();
  std::uint64_t result = 0;
  for (int i = 0; i < runs; ++i) {
    vmp::runtime::vm1::Vm1Context context(module);
    context.bridge_registry = bridge;
    context.audit_dispatcher = dispatcher;
    vmp::runtime::vm1::Vm1Interpreter interpreter;
    result = interpreter.execute(context).ret_int;
  }
  return result;
}

inline bool host_supports_x64_backend() {
#if defined(__linux__) && defined(__x86_64__)
  return true;
#else
  return false;
#endif
}

}  // namespace vmp::tests::runtime_vm1_jit
