#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace vmp::tests::runtime_state {

inline void require(bool cond, const std::string& msg) {
  if (!cond) {
    throw std::runtime_error(msg);
  }
}

inline std::filesystem::path temp_path(const std::string& stem, const std::string& ext) {
  auto path = std::filesystem::temp_directory_path() /
              (stem + "_" + std::to_string(::getpid()) + "_" + std::to_string(std::rand()) + ext);
  return path;
}

inline std::string read_all(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

}  // namespace vmp::tests::runtime_state
