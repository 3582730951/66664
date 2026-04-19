#pragma once

#include <elf.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <vmp/arch/common/lifting.h>

namespace vmp::tests::arch::decoder {

struct TextSection {
  std::uint64_t address = 0;
  std::vector<std::uint8_t> bytes;
};

inline void require(bool cond, const std::string& msg) {
  if (!cond) throw std::runtime_error(msg);
}

inline std::string shell_quote(const std::filesystem::path& path) {
  std::string text = path.string();
  std::string out = "'";
  for (char ch : text) {
    if (ch == '\'') out += "'\\''";
    else out.push_back(ch);
  }
  out.push_back('\'');
  return out;
}

inline std::string shell_quote(std::string_view text) {
  std::string out = "'";
  for (char ch : text) {
    if (ch == '\'') out += "'\\''";
    else out.push_back(ch);
  }
  out.push_back('\'');
  return out;
}

inline void run_command_checked(const std::string& command) {
  const int rc = std::system(command.c_str());
  if (rc != 0) {
    throw std::runtime_error("command failed rc=" + std::to_string(rc) + ": " + command);
  }
}

inline std::vector<std::uint8_t> read_file_bytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  require(static_cast<bool>(input), "failed to open " + path.string());
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

inline TextSection load_text_section(const std::filesystem::path& path, std::size_t max_bytes) {
  const auto bytes = read_file_bytes(path);
  require(bytes.size() >= EI_NIDENT, "ELF file too small");
  require(bytes[0] == 0x7F && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F', "ELF magic mismatch");
  require(bytes[EI_DATA] == ELFDATA2LSB, "little-endian ELF expected");

  if (bytes[EI_CLASS] == ELFCLASS64) {
    require(bytes.size() >= sizeof(Elf64_Ehdr), "short Elf64 header");
    const auto* eh = reinterpret_cast<const Elf64_Ehdr*>(bytes.data());
    require(eh->e_shoff + static_cast<std::uint64_t>(eh->e_shnum) * sizeof(Elf64_Shdr) <= bytes.size(), "short Elf64 sections");
    const auto* sh = reinterpret_cast<const Elf64_Shdr*>(bytes.data() + eh->e_shoff);
    require(eh->e_shstrndx < eh->e_shnum, "invalid Elf64 shstrndx");
    const auto* shstr = reinterpret_cast<const char*>(bytes.data() + sh[eh->e_shstrndx].sh_offset);
    for (std::size_t i = 0; i < eh->e_shnum; ++i) {
      const auto name = std::string(shstr + sh[i].sh_name);
      if (name == ".text") {
        const auto size = std::min<std::size_t>(static_cast<std::size_t>(sh[i].sh_size), max_bytes);
        require(sh[i].sh_offset + size <= bytes.size(), "short Elf64 .text");
        return TextSection{sh[i].sh_addr,
                           std::vector<std::uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(sh[i].sh_offset),
                                                     bytes.begin() + static_cast<std::ptrdiff_t>(sh[i].sh_offset + size))};
      }
    }
  } else if (bytes[EI_CLASS] == ELFCLASS32) {
    require(bytes.size() >= sizeof(Elf32_Ehdr), "short Elf32 header");
    const auto* eh = reinterpret_cast<const Elf32_Ehdr*>(bytes.data());
    require(static_cast<std::size_t>(eh->e_shoff) + static_cast<std::size_t>(eh->e_shnum) * sizeof(Elf32_Shdr) <= bytes.size(), "short Elf32 sections");
    const auto* sh = reinterpret_cast<const Elf32_Shdr*>(bytes.data() + eh->e_shoff);
    require(eh->e_shstrndx < eh->e_shnum, "invalid Elf32 shstrndx");
    const auto* shstr = reinterpret_cast<const char*>(bytes.data() + sh[eh->e_shstrndx].sh_offset);
    for (std::size_t i = 0; i < eh->e_shnum; ++i) {
      const auto name = std::string(shstr + sh[i].sh_name);
      if (name == ".text") {
        const auto size = std::min<std::size_t>(static_cast<std::size_t>(sh[i].sh_size), max_bytes);
        require(static_cast<std::size_t>(sh[i].sh_offset) + size <= bytes.size(), "short Elf32 .text");
        return TextSection{sh[i].sh_addr,
                           std::vector<std::uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(sh[i].sh_offset),
                                                     bytes.begin() + static_cast<std::ptrdiff_t>(sh[i].sh_offset + size))};
      }
    }
  }
  throw std::runtime_error("unsupported ELF class for " + path.string());
}

inline int unsupported_threshold_percent() {
  if (const char* raw = std::getenv("VMP_ISA_TEST_UNSUPPORTED_MAX"); raw != nullptr && *raw != '\0') {
    return std::max(0, std::atoi(raw));
  }
  return 5;
}

inline double percent(std::size_t value, std::size_t total) {
  if (total == 0) return 0.0;
  return static_cast<double>(value) * 100.0 / static_cast<double>(total);
}

inline void require_unsupported_rate(std::string_view label, std::size_t unsupported, std::size_t total) {
  const auto limit = unsupported_threshold_percent();
  const auto rate = percent(unsupported, total);
  std::cout << "NOTE " << label << " unsupported=" << unsupported << "/" << total << " pct="
            << std::fixed << std::setprecision(2) << rate << '\n';
  require(static_cast<int>(std::llround(rate * 100.0)) <= limit * 100,
          std::string(label) + " unsupported rate exceeded limit " + std::to_string(limit) + "%");
}

inline std::filesystem::path write_c_source(const std::filesystem::path& dir, const std::string& name, const std::string& code) {
  std::filesystem::create_directories(dir);
  const auto path = dir / name;
  std::ofstream out(path);
  require(static_cast<bool>(out), "failed to write source " + path.string());
  out << code;
  return path;
}

inline std::filesystem::path compile_c_binary(const std::filesystem::path& dir,
                                              const std::string& compiler,
                                              const std::vector<std::string>& flags,
                                              const std::string& code,
                                              const std::string& stem) {
  const auto source = write_c_source(dir, stem + ".c", code);
  const auto output = dir / stem;
  std::ostringstream cmd;
  cmd << compiler;
  for (const auto& flag : flags) cmd << ' ' << flag;
  cmd << ' ' << shell_quote(source) << " -o " << shell_quote(output);
  run_command_checked(cmd.str());
  return output;
}

inline std::string hello_source() {
  return R"C(
static __attribute__((noinline)) long helper(long a, long b) { return a + b + 1; }
int main(void) { return (int)helper(20, 21); }
)C";
}

}  // namespace vmp::tests::arch::decoder
