#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#include <vmp/runtime/audit/audit.h>
#include <vmp/runtime/audit/reaction.h>
#include <vmp/runtime/bridge/bridge.h>
#include <vmp/runtime/jit/vm2_jit.h>
#include <vmp/runtime/state/state.h>
#include <vmp/runtime/strings/cipher.h>
#include <vmp/runtime/strings/keyctx.h>
#include <vmp/runtime/vm2/vm2.h>

namespace vmp::tests::runtime_vm2_jit {

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

inline std::string read_all(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::ostringstream oss;
  oss << input.rdbuf();
  return oss.str();
}

inline std::vector<std::uint8_t> fixed_master_key_bytes() {
  std::vector<std::uint8_t> key(32);
  for (std::size_t i = 0; i < key.size(); ++i) {
    key[i] = static_cast<std::uint8_t>(0x61 + i);
  }
  return key;
}

inline std::shared_ptr<vmp::runtime::strings::KeyContext> fixed_key_context_ptr(
    std::vector<std::uint8_t> salt = std::vector<std::uint8_t>(16, 0x33)) {
  return std::make_shared<vmp::runtime::strings::KeyContext>(
      vmp::runtime::strings::MasterKeyHandle([] { return fixed_master_key_bytes(); }), std::move(salt));
}

inline std::shared_ptr<vmp::runtime::strings::StringPool> make_string_pool(
    const std::vector<std::pair<std::uint32_t, std::string>>& items,
    std::vector<std::uint8_t>* salt_out = nullptr) {
  auto salt = std::vector<std::uint8_t>(16, 0x33);
  auto key_for_pool = vmp::runtime::strings::KeyContext(
      vmp::runtime::strings::MasterKeyHandle([] { return fixed_master_key_bytes(); }), salt);
  const auto subkey = key_for_pool.derive_subkey("string-pool");
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
  return std::make_shared<vmp::runtime::strings::StringPool>(
      std::move(ciphertext), std::move(index),
      vmp::runtime::strings::KeyContext(
          vmp::runtime::strings::MasterKeyHandle([] { return fixed_master_key_bytes(); }), salt));
}

inline vmp::runtime::vm2::Vm2Module assemble_text(const std::string& text) {
  return vmp::runtime::vm2::assemble_module_text(text);
}

inline vmp::runtime::vm2::ExecutionResult run_module(vmp::runtime::vm2::Vm2Module& module,
                                                     const std::string& backend,
                                                     int runs,
                                                     const std::vector<std::uint64_t>& int_args = {},
                                                     vmp::runtime::bridge::BridgeRegistry* bridge = nullptr,
                                                     vmp::runtime::audit::ReactionDispatcher* dispatcher = nullptr,
                                                     std::shared_ptr<vmp::runtime::strings::StringPool> pool = nullptr,
                                                     std::shared_ptr<vmp::runtime::strings::KeyContext> key_context = nullptr) {
  EnvGuard backend_guard("VMP_JIT_BACKEND", backend);
  auto& jit = vmp::runtime::jit::Vm2Jit::instance();
  jit.reset_for_tests();
  vmp::runtime::vm2::ExecutionResult result{};
  for (int i = 0; i < runs; ++i) {
    vmp::runtime::vm2::Vm2Context context(module);
    for (std::size_t reg = 0; reg < int_args.size() && reg < 8; ++reg) {
      context.r[reg] = int_args[reg];
    }
    const std::size_t spill_count = int_args.size() > 8 ? int_args.size() - 8 : 0u;
    if (spill_count > 0) {
      const auto spill_base = context.allocate_spill(spill_count * sizeof(std::uint64_t), "jit test spill");
      for (std::size_t reg = 0; reg < spill_count; ++reg) {
        context.write_memory<std::uint64_t>(spill_base + static_cast<std::uint64_t>(reg * sizeof(std::uint64_t)),
                                            int_args[8 + reg]);
      }
    }
    context.bridge_registry = bridge;
    context.audit_dispatcher = dispatcher;
    context.string_pool = pool;
    context.key_context = key_context;
    vmp::runtime::vm2::Vm2Interpreter interpreter;
    result = interpreter.execute(context);
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

inline std::string fib20_program() {
  return R"(
entry:
  blnk @fib, 1
  bret

fib:
  ildimm r10, 2
  isub r11, r0, r10
  jp p1, @fib_base
  imov r20, r0
  ildimm r10, 1
  isub r0, r20, r10
  blnk @fib, 1
  imov r21, r0
  ildimm r10, 2
  isub r0, r20, r10
  blnk @fib, 1
  iadd r0, r21, r0
  bret

fib_base:
  bret
)";
}

inline std::uint32_t single_non_entry_function(const vmp::runtime::vm2::Vm2Module& module) {
  for (const auto pc : module.function_entries) {
    if (pc != module.entry_pc) {
      return pc;
    }
  }
  throw std::runtime_error("expected non-entry function");
}

}  // namespace vmp::tests::runtime_vm2_jit
