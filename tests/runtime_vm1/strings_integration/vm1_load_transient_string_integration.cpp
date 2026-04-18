#include "../test_common.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <vmp/runtime/strings/cipher.h>
#include <vmp/runtime/strings/keyctx.h>

using namespace vmp::tests::runtime_vm1;
namespace bridge = vmp::runtime::bridge;
namespace strings = vmp::runtime::strings;
namespace vm1 = vmp::runtime::vm1;

int main() {
  try {
    strings::KeyContext key(strings::MasterKeyHandle([]() {
                            return strings::hex_decode(
                                "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd");
                          }),
                          strings::hex_decode(
                              "0011001100110011001100110011001100110011001100110011001100110011"));
    const auto subkey = key.derive_subkey("string-pool");
    const auto nonce = strings::u32_to_nonce(42);
    const auto rec = strings::encrypt_string_record(subkey.bytes(), nonce, strings::to_bytes("hello from vm1"));

    strings::IndexMap index;
    index.emplace(42, strings::StringIndexEntry{0, static_cast<std::uint32_t>(rec.ciphertext.size()), nonce,
                                                vmp::policy::PlaintextBudget::transient_only});
    auto pool = std::make_shared<strings::StringPool>(rec.ciphertext, index, std::move(key));

    bridge::BridgeRegistry registry;
    std::string captured;
    vm1::Vm1Module module = vm1::assemble_module_text(R"(
entry:
  load_tstr vr5, &sid42
  mov vr0, vr5
  domain_call native, 500, 1
  release_tstr vr5
  ret
)");
    vm1::Vm1Context context(module);
    context.bridge_registry = &registry;
    context.string_pool = pool;

    registry.register_native(500, [&](const bridge::DomainCallArgs& args) {
      captured = context.transient_string(args.ints.at(0));
      return bridge::DomainCallResult{static_cast<std::uint64_t>(captured.size()), 0.0, 0};
    });

    vm1::Vm1Interpreter interpreter;
    const auto result = interpreter.execute(context);
    require_int(result.ret_int, 14, "native return size");
    require(captured == "hello from vm1", "captured string mismatch");
    require(context.active_transient_strings() == 0, "transient handles must be released");
    std::cout << "vm1_load_transient_string_integration OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vm1_load_transient_string_integration failed: " << ex.what() << '\n';
    return 1;
  }
}
