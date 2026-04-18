#include "test_common.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <vector>

#include <vmp/runtime/strings/cipher.h>
#include <vmp/runtime/strings/keyctx.h>

using namespace vmp::tests::strings;
namespace strings = vmp::runtime::strings;

namespace {

struct RawProvider {
  std::vector<std::uint8_t> value;
  std::vector<std::uint8_t> operator()() const { return value; }
};

}  // namespace

int main() {
  try {
    const auto master = strings::hex_decode("00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");
    const auto salt = strings::hex_decode("aabbccddeeff00112233445566778899ffeeddccbbaa99887766554433221100");
    strings::KeyContext key(strings::MasterKeyHandle(RawProvider{master}), salt);
    const auto subkey = key.derive_subkey("string-pool");
    const auto nonce_vec = strings::hex_decode("0102030405060708090a0b0c");
    strings::Nonce nonce{};
    std::copy(nonce_vec.begin(), nonce_vec.end(), nonce.begin());
    const auto record = strings::encrypt_string_record(subkey.bytes(), nonce, strings::to_bytes("wipe-me"));

    strings::IndexMap index;
    index.emplace(7, strings::StringIndexEntry{0, static_cast<std::uint32_t>(record.ciphertext.size()), nonce,
                                               vmp::policy::PlaintextBudget::transient_only});
    strings::StringPool pool(record.ciphertext, index, std::move(key));

    alignas(strings::TransientView) std::array<std::byte, sizeof(strings::TransientView)> storage{};
    auto* view = new (storage.data()) strings::TransientView(pool.decrypt(7));
    require(std::string(view->view()) == "wipe-me", "decrypted content mismatch");
    const auto* ptr = reinterpret_cast<const volatile std::uint8_t*>(view->data());
    const auto len = view->size();
    view->~TransientView();
    std::atomic_signal_fence(std::memory_order_seq_cst);
    for (std::size_t i = 0; i < len; ++i) {
      require(ptr[i] == 0, "transient buffer not zeroized");
    }
    std::cout << "transient_view_zeroization OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "transient_view_zeroization failed: " << ex.what() << '\n';
    return 1;
  }
}
