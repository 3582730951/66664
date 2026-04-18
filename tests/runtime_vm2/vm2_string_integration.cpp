#include "test_common.h"

#include <iostream>

#include <vmp/runtime/strings/cipher.h>
#include <vmp/runtime/strings/keyctx.h>

using namespace vmp::tests::runtime_vm2;
namespace bridge = vmp::runtime::bridge;
namespace strings = vmp::runtime::strings;
namespace vm2 = vmp::runtime::vm2;

namespace {
std::vector<std::uint8_t> make_salt() {
  std::vector<std::uint8_t> salt(32);
  for (std::size_t i = 0; i < salt.size(); ++i) salt[i] = static_cast<std::uint8_t>(i + 1);
  return salt;
}

strings::MasterKeyHandle make_handle() {
  return strings::MasterKeyHandle([]() {
    return strings::hex_decode("00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");
  });
}
}  // namespace

int main() {
  try {
    auto key_for_pool = strings::KeyContext(make_handle(), make_salt());
    const auto subkey = key_for_pool.derive_subkey("string-pool");
    const auto nonce = strings::u32_to_nonce(1);
    const auto record = strings::encrypt_string_record(subkey.bytes(), nonce, strings::to_bytes("hello vm2"));
    strings::IndexMap index{{1, strings::StringIndexEntry{0, static_cast<std::uint32_t>(record.ciphertext.size()), nonce}}};
    auto pool = std::make_shared<strings::StringPool>(record.ciphertext, index, std::move(key_for_pool));
    auto key_for_ctx = std::make_shared<strings::KeyContext>(make_handle(), make_salt());

    bridge::BridgeRegistry registry;
    std::string captured;
    registry.register_native(500, [&](const bridge::DomainCallArgs& args) {
      auto* ctx = static_cast<vm2::Vm2Context*>(args.opaque.at(0));
      captured = ctx->transient_string(args.ints.at(1));
      return bridge::DomainCallResult{static_cast<std::uint64_t>(captured.size()), 0.0, 0};
    });

    auto module = vm2::assemble_module_text(R"(
      tsload r1, 1
      imov r0, r31
      xcall native, 500, 2, 0, 1
      bret
    )");
    vm2::Vm2Context context(module);
    context.string_pool = pool;
    context.key_context = key_for_ctx;
    context.bridge_registry = &registry;
    context.r[31] = reinterpret_cast<std::uintptr_t>(&context);

    vm2::Vm2Interpreter interpreter;
    require_int(interpreter.execute(context).ret_int, 9, "native length");
    require(captured == "hello vm2", "captured string mismatch");
    require(context.active_transient_strings() == 0, "handles should be wiped after return");

    std::cout << "vm2_string_integration OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vm2_string_integration failed: " << ex.what() << '\n';
    return 1;
  }
}
