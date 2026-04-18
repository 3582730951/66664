#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <vmp/policy/policy_ir.h>
#include <vmp/runtime/strings/cipher.h>
#include <vmp/runtime/strings/keyctx.h>

namespace vmp::tools::strings_tool {

using json = nlohmann::json;

struct ProtectedStringEntry {
  std::uint32_t string_id = 0;
  std::string symbol;
  std::string value;
  vmp::policy::PlaintextBudget plaintext_budget = vmp::policy::PlaintextBudget::transient_only;
};

struct ProtectOutputs {
  std::size_t protected_count = 0;
  std::vector<std::uint8_t> salt;
};

inline std::vector<std::uint8_t> read_binary(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to open " + path.string());
  }
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

inline std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to open " + path.string());
  }
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

inline void write_binary(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("failed to create " + path.string());
  }
  output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

inline void write_text(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("failed to create " + path.string());
  }
  output << text;
}

inline std::vector<std::uint8_t> random_bytes(std::size_t size) {
  std::vector<std::uint8_t> out(size);
  std::random_device rd;
  for (auto& byte : out) {
    byte = static_cast<std::uint8_t>(rd());
  }
  return out;
}

inline std::vector<std::uint8_t> resolve_master_key(const std::string& cli_hex = {}) {
  std::string hex = cli_hex;
  if (hex.empty()) {
    if (const char* env = std::getenv("VMP_STRING_MASTER_KEY"); env != nullptr && *env != '\0') {
      hex = env;
    }
  }
  if (!hex.empty()) {
    auto decoded = vmp::runtime::strings::hex_decode(hex);
    if (decoded.size() != 16 && decoded.size() != 32) {
      throw std::runtime_error("master key must decode to 16 or 32 bytes");
    }
    return decoded;
  }

  std::string stdin_hex;
  if (std::cin.good() && !std::cin.eof()) {
    std::getline(std::cin, stdin_hex);
    if (!stdin_hex.empty()) {
      auto decoded = vmp::runtime::strings::hex_decode(stdin_hex);
      if (decoded.size() != 16 && decoded.size() != 32) {
        throw std::runtime_error("stdin master key must decode to 16 or 32 bytes");
      }
      return decoded;
    }
  }

  auto generated = random_bytes(32);
  std::cerr << "generated_master_key=" << vmp::runtime::strings::hex_encode(generated) << '\n';
  return generated;
}

inline std::vector<ProtectedStringEntry> parse_policy_strings(const std::filesystem::path& policy_path) {
  const auto root = json::parse(read_text(policy_path));
  std::vector<ProtectedStringEntry> out;
  if (!root.contains("entries") || !root["entries"].is_array()) {
    return out;
  }
  for (const auto& entry : root["entries"]) {
    bool vm_string = false;
    if (entry.contains("annotation_tags") && entry["annotation_tags"].is_array()) {
      for (const auto& tag : entry["annotation_tags"]) {
        if (tag == "vm_string") {
          vm_string = true;
          break;
        }
      }
    }
    if (!vm_string || !entry.contains("value")) {
      continue;
    }
    ProtectedStringEntry item;
    item.symbol = entry.value("symbol_or_region", "");
    if (entry.contains("string_id")) {
      item.string_id = entry["string_id"].get<std::uint32_t>();
    } else if (item.symbol.rfind("sid", 0) == 0) {
      item.string_id = static_cast<std::uint32_t>(std::stoul(item.symbol.substr(3)));
    } else {
      throw std::runtime_error("vm_string entry requires string_id or sid-prefixed symbol_or_region");
    }
    item.value = entry["value"].get<std::string>();
    const auto budget = entry.value("plaintext_budget", std::string("transient_only"));
    item.plaintext_budget = budget == "none" ? vmp::policy::PlaintextBudget::none
                                              : vmp::policy::PlaintextBudget::transient_only;
    out.push_back(std::move(item));
  }
  return out;
}

inline vmp::runtime::strings::MasterKeyHandle key_from_env(const std::string& env_name) {
  return vmp::runtime::strings::MasterKeyHandle([env_name]() {
    const char* value = std::getenv(env_name.c_str());
    if (value == nullptr || *value == '\0') {
      throw std::runtime_error("missing master key env: " + env_name);
    }
    return vmp::runtime::strings::hex_decode(value);
  });
}

inline ProtectOutputs protect_policy_strings(const std::filesystem::path& policy_path,
                                             const std::filesystem::path& out_bin,
                                             const std::filesystem::path& out_idx,
                                             const std::filesystem::path& out_kdf,
                                             const std::vector<std::uint8_t>& master_key) {
  const auto entries = parse_policy_strings(policy_path);
  const auto salt = random_bytes(32);
  vmp::runtime::strings::KeyContext key(vmp::runtime::strings::MasterKeyHandle([master_key]() { return master_key; }), salt);
  const auto subkey = key.derive_subkey("string-pool");

  std::vector<std::uint8_t> blob;
  json idx_root;
  idx_root["key_context"] = {
      {"salt", vmp::runtime::strings::hex_encode(salt)},
      {"kdf", "HKDF-SHA256"},
      {"purpose_tag", "string-pool"},
      {"master_key_source", "env_or_stdin"},
  };
  json entries_json = json::object();

  for (const auto& entry : entries) {
    const auto nonce = vmp::runtime::strings::u32_to_nonce(entry.string_id);
    const auto record = vmp::runtime::strings::encrypt_string_record(subkey.bytes(), nonce,
                                                                     vmp::runtime::strings::to_bytes(entry.value));
    const auto offset = static_cast<std::uint32_t>(blob.size());
    blob.insert(blob.end(), record.ciphertext.begin(), record.ciphertext.end());
    entries_json[std::to_string(entry.string_id)] = {
        {"offset", offset},
        {"length", static_cast<std::uint32_t>(record.ciphertext.size())},
        {"nonce", vmp::runtime::strings::hex_encode(std::vector<std::uint8_t>(nonce.begin(), nonce.end()))},
        {"plaintext_budget", entry.plaintext_budget == vmp::policy::PlaintextBudget::none ? "none" : "transient_only"},
        {"symbol_or_region", entry.symbol},
    };
  }
  idx_root["entries"] = std::move(entries_json);

  json kdf_meta = {
      {"salt", vmp::runtime::strings::hex_encode(salt)},
      {"kdf", "HKDF-SHA256"},
      {"purpose_tag", "string-pool"},
      {"master_key_source", "env_or_stdin"},
      {"string_count", entries.size()},
  };

  write_binary(out_bin, blob);
  write_text(out_idx, idx_root.dump(2));
  write_text(out_kdf, kdf_meta.dump(2));
  return ProtectOutputs{entries.size(), salt};
}

inline std::pair<vmp::runtime::strings::IndexMap, std::vector<std::uint8_t>> load_index_file(const std::filesystem::path& idx_path) {
  const auto root = json::parse(read_text(idx_path));
  vmp::runtime::strings::IndexMap index;
  const auto& entries = root.at("entries");
  for (auto it = entries.begin(); it != entries.end(); ++it) {
    vmp::runtime::strings::StringIndexEntry entry;
    entry.offset = it.value().at("offset").get<std::uint32_t>();
    entry.length = it.value().at("length").get<std::uint32_t>();
    const auto nonce = vmp::runtime::strings::hex_decode(it.value().at("nonce").get<std::string>());
    if (nonce.size() != vmp::runtime::strings::kChaCha20NonceSize) {
      throw std::runtime_error("invalid nonce size in index json");
    }
    std::copy(nonce.begin(), nonce.end(), entry.nonce.begin());
    const auto budget = it.value().value("plaintext_budget", std::string("transient_only"));
    entry.plaintext_budget = budget == "none" ? vmp::policy::PlaintextBudget::none
                                               : vmp::policy::PlaintextBudget::transient_only;
    index.emplace(static_cast<std::uint32_t>(std::stoul(it.key())), entry);
  }
  const auto salt = vmp::runtime::strings::hex_decode(root.at("key_context").at("salt").get<std::string>());
  return {index, salt};
}

}  // namespace vmp::tools::strings_tool
