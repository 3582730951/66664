#include "test_common.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <vmp/runtime/audit/audit.h>
#include <vmp/runtime/audit/reaction.h>
#include <vmp/runtime/strings/cipher.h>
#include <vmp/runtime/strings/keyctx.h>

using namespace vmp::tests::strings;
namespace audit = vmp::runtime::audit;
namespace strings = vmp::runtime::strings;

int main() {
  try {
    const auto dir = std::filesystem::temp_directory_path() / "vmp_pool_corruption_detected";
    std::filesystem::create_directories(dir);
    const auto log = dir / "audit.log";
    audit::AuditWriter writer(log);
    audit::ReactionDispatcher dispatcher(writer, audit::ReactionPolicy::audit_only);

    strings::KeyContext key(strings::MasterKeyHandle([]() {
                            return strings::hex_decode(
                                "0101010101010101010101010101010101010101010101010101010101010101");
                          }),
                          strings::hex_decode(
                              "0202020202020202020202020202020202020202020202020202020202020202"));
    const auto subkey = key.derive_subkey("string-pool");
    const auto nonce = strings::u32_to_nonce(99);
    auto rec = strings::encrypt_string_record(subkey.bytes(), nonce, strings::to_bytes("tamper"));
    rec.ciphertext[3] ^= 0x80;

    strings::IndexMap index;
    index.emplace(9, strings::StringIndexEntry{0, static_cast<std::uint32_t>(rec.ciphertext.size()), nonce,
                                               vmp::policy::PlaintextBudget::transient_only});
    strings::StringPool pool(rec.ciphertext, index, std::move(key));
    pool.set_audit_dispatcher(&dispatcher);

    bool failed = false;
    try {
      auto view = pool.decrypt(9);
      (void)view;
    } catch (const std::exception&) {
      failed = true;
    }
    writer.flush();
    require(failed, "corrupted pool should fail");

    std::ifstream input(log);
    const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    require(content.find("string_pool_error") != std::string::npos, "audit missing string_pool_error");
    std::cout << "pool_corruption_detected OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "pool_corruption_detected failed: " << ex.what() << '\n';
    return 1;
  }
}
