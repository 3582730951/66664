#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <vmp/runtime/vm1/vm1.h>

namespace {
std::string slurp(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to open asm input: " + path);
  }
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> slurp_bytes(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to open module input: " + path);
  }
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

std::string hex_u32(std::uint32_t value) {
  std::ostringstream oss;
  oss << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
  return oss.str();
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 3 && std::string(argv[1]) == "--crc-only") {
      const auto bytes = slurp_bytes(argv[2]);
      std::cout << hex_u32(vmp::runtime::vm1::serialized_body_crc32(bytes)) << "\n";
      return 0;
    }
    if (argc != 3) {
      std::cerr << "usage: vmp-vm1-asm <input.vm1s> <output.vm1>\n";
      std::cerr << "       vmp-vm1-asm --crc-only <module.vm1>\n";
      return 2;
    }
    const auto module = vmp::runtime::vm1::assemble_module_text(slurp(argv[1]));
    module.save_to_file(argv[2]);
    std::cout << "assembled " << argv[1] << " -> " << argv[2] << "\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vmp-vm1-asm failed: " << ex.what() << '\n';
    return 1;
  }
}
