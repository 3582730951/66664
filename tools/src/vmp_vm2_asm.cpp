#include <fstream>
#include <iostream>
#include <string>

#include <vmp/runtime/vm2/vm2.h>

namespace {
std::string slurp(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("failed to open asm input: " + path);
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}
}

int main(int argc, char** argv) {
  try {
    if (argc != 3) {
      std::cerr << "usage: vmp-vm2-asm <input.vm2s> <output.vm2>\n";
      return 2;
    }
    const auto module = vmp::runtime::vm2::assemble_module_text(slurp(argv[1]));
    module.save_to_file(argv[2]);
    std::cout << "assembled " << argv[1] << " -> " << argv[2] << "\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vmp-vm2-asm failed: " << ex.what() << '\n';
    return 1;
  }
}
