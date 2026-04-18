#include "test_common.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <vmp/runtime/strings/cipher.h>
#include <vmp/runtime/strings/keyctx.h>

using namespace vmp::tests::strings;
namespace strings = vmp::runtime::strings;

namespace {

std::vector<std::uint8_t> hex(std::string_view input) {
  return strings::hex_decode(std::string(input));
}

void require_eq(const std::vector<std::uint8_t>& actual, const std::vector<std::uint8_t>& expected,
                const std::string& label) {
  require(actual == expected, label + " mismatch");
}

void test_chacha20_rfc_vectors() {
  const auto key = hex("0000000000000000000000000000000000000000000000000000000000000000");
  const auto nonce_vec = hex("000000000000000000000000");
  strings::Nonce nonce{};
  std::copy(nonce_vec.begin(), nonce_vec.end(), nonce.begin());
  std::vector<std::uint8_t> plaintext(64, 0);
  const auto expected = hex(
      "76b8e0ada0f13d90405d6ae55386bd28"
      "bdd219b8a08ded1aa836efcc8b770dc7"
      "da41597c5157488d7724e03fb8d84a37"
      "6a43b8f41518a11cc387b669b2ee6586");
  require_eq(strings::chacha20_xor(key, nonce, 0, plaintext), expected, "chacha20_rfc_vectors");
}

void test_sha256_nist_vectors() {
  require(strings::hex_encode(strings::sha256({})) ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "sha256 empty");
  require(strings::hex_encode(strings::sha256(strings::to_bytes("abc"))) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "sha256 abc");
  require(strings::hex_encode(strings::sha256(strings::to_bytes(
              "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))) ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "sha256 long");
}

void test_hkdf_rfc5869_vectors() {
  {
    const auto ikm = hex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
    const auto salt = hex("000102030405060708090a0b0c");
    const auto info = hex("f0f1f2f3f4f5f6f7f8f9");
    const auto prk = strings::hkdf_extract_sha256(salt, ikm);
    require(strings::hex_encode(prk) ==
                "077709362c2e32df0ddc3f0dc47bba63"
                "90b6c73bb50f9c3122ec844ad7c2b3e5",
            "hkdf prk case1");
    const auto okm = strings::hkdf_expand_sha256(prk, info, 42);
    require(strings::hex_encode(okm) ==
                "3cb25f25faacd57a90434f64d0362f2a"
                "2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
                "34007208d5b887185865",
            "hkdf okm case1");
  }
  {
    const auto ikm = hex(
        "000102030405060708090a0b0c0d0e0f"
        "101112131415161718191a1b1c1d1e1f"
        "202122232425262728292a2b2c2d2e2f"
        "303132333435363738393a3b3c3d3e3f"
        "404142434445464748494a4b4c4d4e4f");
    const auto salt = hex(
        "606162636465666768696a6b6c6d6e6f"
        "707172737475767778797a7b7c7d7e7f"
        "808182838485868788898a8b8c8d8e8f"
        "909192939495969798999a9b9c9d9e9f"
        "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf");
    const auto info = hex(
        "b0b1b2b3b4b5b6b7b8b9babbbcbdbebf"
        "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf"
        "d0d1d2d3d4d5d6d7d8d9dadbdcdddedf"
        "e0e1e2e3e4e5e6e7e8e9eaebecedeeef"
        "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    const auto prk = strings::hkdf_extract_sha256(salt, ikm);
    require(strings::hex_encode(prk) ==
                "06a6b88c5853361a06104c9ceb35b45c"
                "ef760014904671014a193f40c15fc244",
            "hkdf prk case2");
    const auto okm = strings::hkdf_expand_sha256(prk, info, 82);
    require(strings::hex_encode(okm) ==
                "b11e398dc80327a1c8e7f78c596a4934"
                "4f012eda2d4efad8a050cc4c19afa97c"
                "59045a99cac7827271cb41c65e590e09"
                "da3275600c2f09b8367793a9aca3db71"
                "cc30c58179ec3e87c14c01d5c1f3434f"
                "1d87",
            "hkdf okm case2");
  }
}

}  // namespace

int main() {
  try {
    test_chacha20_rfc_vectors();
    test_sha256_nist_vectors();
    test_hkdf_rfc5869_vectors();
    std::cout << "crypto_vectors OK\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "crypto_vectors failed: " << ex.what() << '\n';
    return 1;
  }
}
