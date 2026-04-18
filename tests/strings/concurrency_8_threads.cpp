#include "test_common.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <vmp/runtime/strings/cipher.h>
#include <vmp/runtime/strings/keyctx.h>

using namespace vmp::tests::strings;
namespace strings = vmp::runtime::strings;

namespace {

std::vector<std::uint8_t> make_master() {
  return strings::hex_decode("1111111111111111111111111111111111111111111111111111111111111111");
}

std::vector<std::uint8_t> make_salt() {
  return strings::hex_decode("2222222222222222222222222222222222222222222222222222222222222222");
}

}  // namespace

int main() {
  try {
    const auto master = make_master();
    strings::KeyContext key(strings::MasterKeyHandle([master]() { return master; }), make_salt());
    const auto subkey = key.derive_subkey("string-pool");

    std::vector<std::uint8_t> blob;
    strings::IndexMap index;
    std::vector<std::string> values;
    for (std::uint32_t id = 0; id < 16; ++id) {
      const auto nonce = strings::u32_to_nonce(id + 1);
      values.push_back("thread-value-" + std::to_string(id));
      const auto rec = strings::encrypt_string_record(subkey.bytes(), nonce, strings::to_bytes(values.back()));
      const auto offset = static_cast<std::uint32_t>(blob.size());
      blob.insert(blob.end(), rec.ciphertext.begin(), rec.ciphertext.end());
      index.emplace(id, strings::StringIndexEntry{offset, static_cast<std::uint32_t>(rec.ciphertext.size()), nonce,
                                                  vmp::policy::PlaintextBudget::transient_only});
    }

    strings::StringPool pool(blob, index, std::move(key));
    std::atomic<bool> failed{false};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
      threads.emplace_back([&]() {
        try {
          for (std::uint32_t id = 0; id < 16; ++id) {
            auto view = pool.decrypt(id);
            if (std::string(view.view()) != values[id]) {
              failed.store(true);
            }
          }
        } catch (...) {
          failed.store(true);
        }
      });
    }
    for (auto& thread : threads) {
      thread.join();
    }
    require(!failed.load(), "concurrency check failed");
    std::cout << "concurrency_8_threads OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "concurrency_8_threads failed: " << ex.what() << '\n';
    return 1;
  }
}
