#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

namespace vmp::tests::runtime_integrity {

inline void require(bool cond, const std::string& msg) {
  if (!cond) {
    throw std::runtime_error(msg);
  }
}

inline std::filesystem::path temp_path(const std::string& stem, const std::string& ext) {
  return std::filesystem::temp_directory_path() /
         (stem + "_" + std::to_string(::getpid()) + "_" + std::to_string(std::rand()) + ext);
}

inline std::string read_all(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

inline std::string shell_quote(const std::string& s) {
  std::string out = "'";
  for (char ch : s) {
    out += (ch == '\'' ? "'\\''" : std::string(1, ch));
  }
  out += "'";
  return out;
}

class ScopedEnvVar {
 public:
  ScopedEnvVar(std::string key, std::string value) : key_(std::move(key)) {
    const char* existing = std::getenv(key_.c_str());
    if (existing != nullptr) {
      old_ = std::string(existing);
    }
    ::setenv(key_.c_str(), value.c_str(), 1);
  }

  ~ScopedEnvVar() {
    if (old_.has_value()) {
      ::setenv(key_.c_str(), old_->c_str(), 1);
    } else {
      ::unsetenv(key_.c_str());
    }
  }

  ScopedEnvVar(const ScopedEnvVar&) = delete;
  ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;

 private:
  std::string key_;
  std::optional<std::string> old_;
};

}  // namespace vmp::tests::runtime_integrity
