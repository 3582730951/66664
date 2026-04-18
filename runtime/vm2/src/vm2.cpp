#include <vmp/runtime/vm2/vm2.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace vmp::runtime::vm2 {
namespace {
using ByteVector = std::vector<std::uint8_t>;
std::atomic<std::uint64_t> g_next_vm2_module_id{1};

struct MemoryOperand {
  std::uint8_t base = 0;
  std::int32_t offset = 0;
};

struct InstructionLine {
  std::string op;
  std::vector<std::string> operands;
  std::uint32_t pc = 0;
};

struct ParsedProgram {
  std::vector<InstructionLine> instructions;
  std::unordered_map<std::string, std::uint32_t> labels;
  std::vector<Vm2ConstPoolEntry> const_pool;
  std::unordered_map<std::string, std::uint32_t> const_labels;
  std::array<std::uint8_t, kVm2KeyContextIdSize> key_context_id{};
};

std::string trim(std::string_view value) {
  std::size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }
  std::size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }
  return std::string(value.substr(begin, end - begin));
}

std::string strip_comment(std::string_view value) {
  bool in_string = false;
  for (std::size_t i = 0; i < value.size(); ++i) {
    const char ch = value[i];
    if (ch == '"' && (i == 0 || value[i - 1] != '\\')) {
      in_string = !in_string;
    }
    if (!in_string && (ch == ';' || ch == '#')) {
      return std::string(value.substr(0, i));
    }
  }
  return std::string(value);
}

std::vector<std::string> split_operands(const std::string& text) {
  std::vector<std::string> out;
  std::string current;
  bool in_string = false;
  int bracket_depth = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char ch = text[i];
    if (ch == '"' && (i == 0 || text[i - 1] != '\\')) {
      in_string = !in_string;
      current.push_back(ch);
      continue;
    }
    if (!in_string) {
      if (ch == '[') {
        ++bracket_depth;
      } else if (ch == ']') {
        --bracket_depth;
      } else if (ch == ',' && bracket_depth == 0) {
        out.push_back(trim(current));
        current.clear();
        continue;
      }
    }
    current.push_back(ch);
  }
  if (!trim(current).empty()) {
    out.push_back(trim(current));
  }
  return out;
}

void append_u16(ByteVector& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void append_u32(ByteVector& out, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu));
  }
}

void append_u64(ByteVector& out, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu));
  }
}

void append_i32(ByteVector& out, std::int32_t value) { append_u32(out, static_cast<std::uint32_t>(value)); }

std::uint16_t read_u16(const ByteVector& bytes, std::size_t& offset) {
  if (offset + 2 > bytes.size()) {
    throw std::runtime_error("vm2: truncated u16");
  }
  const auto value = static_cast<std::uint16_t>(bytes[offset]) |
                     static_cast<std::uint16_t>(bytes[offset + 1] << 8u);
  offset += 2;
  return value;
}

std::uint32_t read_u32(const ByteVector& bytes, std::size_t& offset) {
  if (offset + 4 > bytes.size()) {
    throw std::runtime_error("vm2: truncated u32");
  }
  std::uint32_t value = 0;
  for (int i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(bytes[offset + static_cast<std::size_t>(i)]) << (8 * i);
  }
  offset += 4;
  return value;
}

std::uint64_t read_u64(const ByteVector& bytes, std::size_t& offset) {
  if (offset + 8 > bytes.size()) {
    throw std::runtime_error("vm2: truncated u64");
  }
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(bytes[offset + static_cast<std::size_t>(i)]) << (8 * i);
  }
  offset += 8;
  return value;
}

std::int32_t read_i32(const ByteVector& bytes, std::size_t& offset) {
  return static_cast<std::int32_t>(read_u32(bytes, offset));
}

std::size_t decode_instruction_size(const ByteVector& code, std::size_t pc) {
  std::size_t cursor = pc;
  const auto opcode = static_cast<Opcode>(read_u16(code, cursor));
  switch (opcode) {
    case Opcode::nop:
    case Opcode::brk:
    case Opcode::bret:
    case Opcode::pret:
    case Opcode::xret:
      return 2;
    case Opcode::ftrap:
      return 6;
    case Opcode::ildimm:
      return 11;
    case Opcode::vldimm:
      return 7;
    case Opcode::imov:
    case Opcode::ineg:
    case Opcode::inot:
      return 4;
    case Opcode::tsrelease:
      return 3;
    case Opcode::iadd:
    case Opcode::isub:
    case Opcode::imul:
    case Opcode::idiv:
    case Opcode::imod:
    case Opcode::iand:
    case Opcode::ior:
    case Opcode::ixor:
    case Opcode::ishl:
    case Opcode::ishr:
    case Opcode::isar:
    case Opcode::vadd128:
    case Opcode::vsub128:
    case Opcode::vmul128:
    case Opcode::vxor128:
      return 5;
    case Opcode::imemld8:
    case Opcode::imemld16:
    case Opcode::imemld32:
    case Opcode::imemld64:
    case Opcode::imemst8:
    case Opcode::imemst16:
    case Opcode::imemst32:
    case Opcode::imemst64:
    case Opcode::vmemld128:
    case Opcode::vmemst128:
      return 8;
    case Opcode::jmp:
      return 6;
    case Opcode::jp:
    case Opcode::jnp:
      return 7;
    case Opcode::blnk:
      return 7;
    case Opcode::pcall:
      return 8;
    case Opcode::xcall:
      return 10;
    case Opcode::tsload:
      return 7;
  }
  throw std::runtime_error("vm2: unknown opcode while collecting function entries");
}

std::unordered_set<std::uint32_t> collect_function_entries(const ByteVector& code, std::uint32_t entry_pc) {
  std::unordered_set<std::uint32_t> entries{entry_pc};
  std::size_t pc = 0;
  while (pc < code.size()) {
    std::size_t cursor = pc;
    const auto opcode = static_cast<Opcode>(read_u16(code, cursor));
    switch (opcode) {
      case Opcode::blnk: {
        const auto target = read_u32(code, cursor);
        entries.insert(target);
        break;
      }
      case Opcode::pcall: {
        ++cursor;
        const auto target = read_u32(code, cursor);
        entries.insert(target);
        break;
      }
      default:
        break;
    }
    pc += decode_instruction_size(code, pc);
  }
  return entries;
}

std::int64_t parse_i64(const std::string& text) {
  std::size_t idx = 0;
  int base = 10;
  if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    base = 16;
  } else if (text.size() > 3 && text[0] == '-' && text[1] == '0' && (text[2] == 'x' || text[2] == 'X')) {
    base = 16;
  }
  long long value = std::stoll(text, &idx, base);
  if (idx != text.size()) {
    throw std::runtime_error("vm2 asm: invalid integer '" + text + "'");
  }
  return static_cast<std::int64_t>(value);
}

std::uint64_t parse_u64_value(const std::string& text) {
  std::size_t idx = 0;
  int base = 10;
  if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    base = 16;
  }
  const auto value = std::stoull(text, &idx, base);
  if (idx != text.size()) {
    throw std::runtime_error("vm2 asm: invalid unsigned integer '" + text + "'");
  }
  return static_cast<std::uint64_t>(value);
}

double parse_double_value(const std::string& text) {
  std::size_t idx = 0;
  const auto value = std::stod(text, &idx);
  if (idx != text.size()) {
    throw std::runtime_error("vm2 asm: invalid double '" + text + "'");
  }
  return value;
}

std::uint64_t bit_cast_u64(double value) {
  std::uint64_t out = 0;
  std::memcpy(&out, &value, sizeof(out));
  return out;
}

double bit_cast_double(std::uint64_t value) {
  double out = 0.0;
  std::memcpy(&out, &value, sizeof(out));
  return out;
}

std::uint8_t parse_general_register(const std::string& token) {
  if (token.size() < 2 || token[0] != 'r') {
    throw std::runtime_error("vm2 asm: expected integer register, got '" + token + "'");
  }
  const auto value = static_cast<std::uint32_t>(parse_u64_value(token.substr(1)));
  if (!is_valid_general_register(static_cast<std::uint8_t>(value))) {
    throw std::runtime_error("vm2 asm: integer register out of range");
  }
  return static_cast<std::uint8_t>(value);
}

std::uint8_t parse_vector_register(const std::string& token) {
  if (token.size() < 2 || token[0] != 'q') {
    throw std::runtime_error("vm2 asm: expected vector register, got '" + token + "'");
  }
  const auto value = static_cast<std::uint32_t>(parse_u64_value(token.substr(1)));
  if (!is_valid_vector_register(static_cast<std::uint8_t>(value))) {
    throw std::runtime_error("vm2 asm: vector register out of range");
  }
  return static_cast<std::uint8_t>(value);
}

std::uint8_t parse_float_register(const std::string& token) {
  if (token.size() < 2 || token[0] != 'd') {
    throw std::runtime_error("vm2 asm: expected float register, got '" + token + "'");
  }
  const auto value = static_cast<std::uint32_t>(parse_u64_value(token.substr(1)));
  if (!is_valid_float_register(static_cast<std::uint8_t>(value))) {
    throw std::runtime_error("vm2 asm: float register out of range");
  }
  return static_cast<std::uint8_t>(value);
}

std::uint8_t parse_predicate(const std::string& token) {
  if (token.size() < 2 || token[0] != 'p') {
    throw std::runtime_error("vm2 asm: expected predicate register, got '" + token + "'");
  }
  const auto value = static_cast<std::uint32_t>(parse_u64_value(token.substr(1)));
  if (!is_valid_predicate(static_cast<std::uint8_t>(value))) {
    throw std::runtime_error("vm2 asm: predicate out of range");
  }
  return static_cast<std::uint8_t>(value);
}

MemoryOperand parse_memory_operand(const std::string& token) {
  if (token.size() < 4 || token.front() != '[' || token.back() != ']') {
    throw std::runtime_error("vm2 asm: expected memory operand, got '" + token + "'");
  }
  const auto inner = token.substr(1, token.size() - 2);
  const auto plus = inner.find_first_of("+-", 1);
  const std::string base_text = plus == std::string::npos ? inner : inner.substr(0, plus);
  const std::string offset_text = plus == std::string::npos ? "+0" : inner.substr(plus);
  MemoryOperand operand;
  if (base_text == "sp") {
    operand.base = static_cast<std::uint8_t>(MemoryBase::sp);
  } else {
    operand.base = parse_general_register(base_text);
  }
  operand.offset = static_cast<std::int32_t>(parse_i64(offset_text));
  return operand;
}

std::uint8_t parse_domain_token(const std::string& token) {
  if (token == "native") return 0;
  if (token == "vm1") return 1;
  if (token == "vm2") return 2;
  throw std::runtime_error("vm2 asm: unknown domain '" + token + "'");
}

std::array<std::uint8_t, kVm2KeyContextIdSize> parse_keyctx_hex(const std::string& token) {
  auto hex = token;
  if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0) {
    hex = hex.substr(2);
  }
  if (hex.size() != kVm2KeyContextIdSize * 2) {
    throw std::runtime_error("vm2 asm: keyctx expects 16-byte hex");
  }
  std::array<std::uint8_t, kVm2KeyContextIdSize> out{};
  for (std::size_t i = 0; i < out.size(); ++i) {
    const auto part = hex.substr(i * 2, 2);
    out[i] = static_cast<std::uint8_t>(std::stoul(part, nullptr, 16));
  }
  return out;
}

std::uint32_t instruction_size(const InstructionLine& inst) {
  const auto op = inst.op;
  if (op == "nop" || op == "brk" || op == "bret" || op == "pret" || op == "xret") return 2;
  if (op == "ftrap") return 6;
  if (op == "ildimm") return 11;
  if (op == "vldimm") return 7;
  if (op == "imov" || op == "ineg" || op == "inot") return 4;
  if (op == "tsrelease") return 3;
  if (op == "iadd" || op == "isub" || op == "imul" || op == "idiv" || op == "imod" || op == "iand" ||
      op == "ior" || op == "ixor" || op == "ishl" || op == "ishr" || op == "isar") return 5;
  if (op == "vadd128" || op == "vsub128" || op == "vmul128" || op == "vxor128") return 5;
  if (op == "imemld8" || op == "imemld16" || op == "imemld32" || op == "imemld64") return 8;
  if (op == "imemst8" || op == "imemst16" || op == "imemst32" || op == "imemst64") return 8;
  if (op == "vmemld128" || op == "vmemst128") return 8;
  if (op == "jmp") return 6;
  if (op == "jp" || op == "jnp") return 7;
  if (op == "blnk") return 7;
  if (op == "pcall") return 8;
  if (op == "xcall") return 10;
  if (op == "tsload") return 8;
  throw std::runtime_error("vm2 asm: unknown opcode '" + op + "'");
}

Opcode parse_opcode(const std::string& op) {
  static const std::unordered_map<std::string, Opcode> map = {
      {"nop", Opcode::nop},       {"brk", Opcode::brk},           {"ftrap", Opcode::ftrap},
      {"ildimm", Opcode::ildimm}, {"vldimm", Opcode::vldimm},     {"imov", Opcode::imov},
      {"iadd", Opcode::iadd},     {"isub", Opcode::isub},         {"imul", Opcode::imul},
      {"idiv", Opcode::idiv},     {"imod", Opcode::imod},         {"iand", Opcode::iand},
      {"ior", Opcode::ior},       {"ixor", Opcode::ixor},         {"ishl", Opcode::ishl},
      {"ishr", Opcode::ishr},     {"isar", Opcode::isar},         {"ineg", Opcode::ineg},
      {"inot", Opcode::inot},     {"vadd128", Opcode::vadd128},   {"vsub128", Opcode::vsub128},
      {"vmul128", Opcode::vmul128}, {"vxor128", Opcode::vxor128}, {"imemld8", Opcode::imemld8},
      {"imemld16", Opcode::imemld16}, {"imemld32", Opcode::imemld32}, {"imemld64", Opcode::imemld64},
      {"imemst8", Opcode::imemst8}, {"imemst16", Opcode::imemst16}, {"imemst32", Opcode::imemst32},
      {"imemst64", Opcode::imemst64}, {"vmemld128", Opcode::vmemld128}, {"vmemst128", Opcode::vmemst128},
      {"jmp", Opcode::jmp},       {"jp", Opcode::jp},             {"jnp", Opcode::jnp},
      {"blnk", Opcode::blnk},     {"bret", Opcode::bret},         {"pcall", Opcode::pcall},
      {"pret", Opcode::pret},     {"xcall", Opcode::xcall},       {"xret", Opcode::xret},
      {"tsload", Opcode::tsload}, {"tsrelease", Opcode::tsrelease},
  };
  const auto it = map.find(op);
  if (it == map.end()) throw std::runtime_error("vm2 asm: unknown opcode '" + op + "'");
  return it->second;
}

std::uint32_t resolve_target(const std::string& token, const std::unordered_map<std::string, std::uint32_t>& labels) {
  if (!token.empty() && token[0] == '@') {
    const auto name = token.substr(1);
    const auto it = labels.find(name);
    if (it == labels.end()) {
      throw std::runtime_error("vm2 asm: undefined label '" + name + "'");
    }
    return it->second;
  }
  return static_cast<std::uint32_t>(parse_u64_value(token));
}

std::uint32_t resolve_const(const std::string& token, const std::unordered_map<std::string, std::uint32_t>& const_labels) {
  auto key = token;
  if (!key.empty() && key[0] == '&') key = key.substr(1);
  const auto it = const_labels.find(key);
  if (it != const_labels.end()) return it->second;
  return static_cast<std::uint32_t>(parse_u64_value(key));
}

std::string make_label(std::uint32_t pc) {
  std::ostringstream oss;
  oss << 'L' << std::hex << std::setw(4) << std::setfill('0') << pc;
  return oss.str();
}

std::string base_name(std::uint8_t base) {
  if (base == static_cast<std::uint8_t>(MemoryBase::sp)) return "sp";
  std::ostringstream oss;
  oss << 'r' << static_cast<unsigned>(base);
  return oss.str();
}

std::string memory_operand_text(std::uint8_t base, std::int32_t offset) {
  std::ostringstream oss;
  oss << '[' << base_name(base);
  if (offset >= 0) {
    oss << '+' << offset;
  } else {
    oss << offset;
  }
  oss << ']';
  return oss.str();
}

ParsedProgram parse_assembly(std::string_view text) {
  ParsedProgram program;
  std::uint32_t pc = 0;
  std::istringstream input{std::string(text)};
  std::string raw_line;
  while (std::getline(input, raw_line)) {
    auto line = trim(strip_comment(raw_line));
    if (line.empty()) continue;
    if (line.back() == ':') {
      program.labels.emplace(line.substr(0, line.size() - 1), pc);
      continue;
    }
    if (line.rfind(".keyctx", 0) == 0) {
      const auto args = split_operands(trim(line.substr(7)));
      if (args.size() != 1) throw std::runtime_error("vm2 asm: .keyctx expects one operand");
      program.key_context_id = parse_keyctx_hex(args[0]);
      continue;
    }
    if (line.rfind(".vconst", 0) == 0) {
      const auto args = split_operands(trim(line.substr(7)));
      if (args.size() != 3) throw std::runtime_error("vm2 asm: .vconst expects name, lo, hi");
      Vm2ConstPoolEntry entry{};
      const auto lo = parse_u64_value(args[1]);
      const auto hi = parse_u64_value(args[2]);
      for (int i = 0; i < 8; ++i) entry.bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((lo >> (8 * i)) & 0xFFu);
      for (int i = 0; i < 8; ++i) entry.bytes[8 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((hi >> (8 * i)) & 0xFFu);
      const auto index = static_cast<std::uint32_t>(program.const_pool.size());
      program.const_labels.emplace(args[0], index);
      program.const_pool.push_back(entry);
      continue;
    }
    std::string op;
    std::string operand_text;
    const auto space = line.find_first_of(" \t");
    if (space == std::string::npos) {
      op = line;
    } else {
      op = line.substr(0, space);
      operand_text = trim(line.substr(space + 1));
    }
    InstructionLine inst{op, split_operands(operand_text), pc};
    pc += instruction_size(inst);
    program.instructions.push_back(std::move(inst));
  }
  return program;
}

}  // namespace

Vm2Exception::Vm2Exception(std::uint32_t pc, std::string message) : std::runtime_error(std::move(message)), pc_(pc) {}

Vm2Context::Vm2Context(const Vm2Module& module_in, std::size_t stack_size)
    : pc(module_in.entry_pc), sp(stack_size), module(&module_in), stack_(stack_size, 0) {
  if ((sp & 0xFu) != 0) {
    sp &= ~std::uint64_t(0xFu);
  }
}

std::size_t Vm2Context::stack_size() const noexcept { return stack_.size(); }

void Vm2Context::set_sp(std::uint64_t value) {
  if (value > stack_.size()) throw Vm2StackOverflow(pc, "vm2: sp out of range");
  if ((value & 0xFu) != 0) throw Vm2StackOverflow(pc, "vm2: sp must remain 16-byte aligned");
  sp = value;
}

void Vm2Context::ensure_memory_range(std::uint64_t address, std::size_t width) const {
  if (address > stack_.size() || width > stack_.size() || address + width > stack_.size()) {
    throw Vm2Exception(pc, "vm2: memory access out of bounds");
  }
}

Vec128 Vm2Context::read_vec128(std::uint64_t address) const {
  Vec128 value{};
  value.u64.lo = read_memory<std::uint64_t>(address);
  value.u64.hi = read_memory<std::uint64_t>(address + 8);
  return value;
}

void Vm2Context::write_vec128(std::uint64_t address, const Vec128& value) {
  write_memory<std::uint64_t>(address, value.u64.lo);
  write_memory<std::uint64_t>(address + 8, value.u64.hi);
}

std::uint64_t Vm2Context::allocate_spill(std::size_t bytes, const char* reason) {
  const auto aligned = (bytes + 15u) & ~std::size_t(15u);
  if (aligned > sp) {
    throw Vm2StackOverflow(pc, std::string("vm2: stack overflow during ") + reason);
  }
  sp -= static_cast<std::uint64_t>(aligned);
  return sp;
}

std::array<std::uint8_t, kVm2KeyContextIdSize> Vm2Context::current_key_context_id() const {
  std::array<std::uint8_t, kVm2KeyContextIdSize> out{};
  if (!key_context) return out;
  auto subkey = key_context->derive_subkey("vm2-key-context-id");
  std::vector<std::uint8_t> material(subkey.bytes().begin(), subkey.bytes().end());
  const auto digest = vmp::runtime::strings::sha256(material);
  std::copy_n(digest.begin(), out.size(), out.begin());
  return out;
}

std::uint64_t Vm2Context::materialize_transient_string(std::uint32_t id) {
  if (!string_pool) {
    throw Vm2Exception(pc, "vm2: string pool not configured");
  }
  if (module != nullptr) {
    const auto actual = current_key_context_id();
    if (module->key_context_id != std::array<std::uint8_t, kVm2KeyContextIdSize>{} && actual != module->key_context_id) {
      if (audit_dispatcher != nullptr) {
        audit_dispatcher->dispatch(vmp::runtime::audit::make_event("string_pool_error", "vm2 key context mismatch", pc, "vm2"),
                                   vmp::runtime::audit::ReactionPolicy::audit_only);
      }
      throw Vm2Exception(pc, "vm2: key context mismatch");
    }
  }
  string_pool->set_audit_dispatcher(audit_dispatcher);
  auto view = string_pool->decrypt(id);
  const auto handle = next_transient_handle_++;
  transient_strings_[handle] = std::make_unique<vmp::runtime::strings::TransientView>(std::move(view));
  register_transient_handle(handle);
  return handle;
}

void Vm2Context::release_transient_string(std::uint64_t handle) {
  const auto it = transient_strings_.find(handle);
  if (it == transient_strings_.end()) {
    throw Vm2Exception(pc, "vm2: transient string handle not found");
  }
  remove_transient_handle_owner(handle);
  transient_strings_.erase(it);
}

std::string Vm2Context::transient_string(std::uint64_t handle) const {
  const auto it = transient_strings_.find(handle);
  if (it == transient_strings_.end() || it->second == nullptr) {
    throw Vm2Exception(pc, "vm2: transient string handle not found");
  }
  return std::string(it->second->view());
}

std::size_t Vm2Context::active_transient_strings() const noexcept { return transient_strings_.size(); }

void Vm2Context::register_transient_handle(std::uint64_t handle) {
  if (frames_.empty()) {
    root_transient_handles_.push_back(handle);
  } else {
    frames_.back().transient_handles.push_back(handle);
  }
}

void Vm2Context::remove_transient_handle_owner(std::uint64_t handle) {
  auto erase_from = [handle](std::vector<std::uint64_t>& handles) {
    handles.erase(std::remove(handles.begin(), handles.end(), handle), handles.end());
  };
  erase_from(root_transient_handles_);
  for (auto& frame : frames_) erase_from(frame.transient_handles);
}

void Vm2Context::clear_frame_transient_strings() {
  std::vector<std::uint64_t> handles;
  if (frames_.empty()) {
    handles.swap(root_transient_handles_);
  } else {
    handles.swap(frames_.back().transient_handles);
  }
  for (const auto handle : handles) transient_strings_.erase(handle);
}

void Vm2Context::clear_all_transient_strings() noexcept {
  transient_strings_.clear();
  root_transient_handles_.clear();
  for (auto& frame : frames_) frame.transient_handles.clear();
}

Vm2Module Vm2Module::load_from_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("vm2: failed to open module '" + path + "'");
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  return load_from_bytes(bytes);
}

Vm2Module Vm2Module::load_from_bytes(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < 36) throw std::runtime_error("vm2: module too small");
  Vm2Module module;
  if (!std::equal(kVm2Magic.begin(), kVm2Magic.end(), bytes.begin())) throw std::runtime_error("vm2: bad magic");
  std::size_t offset = 4;
  module.version = read_u16(bytes, offset);
  module.module_flags = read_u16(bytes, offset);
  module.entry_pc = read_u32(bytes, offset);
  const auto code_size = read_u32(bytes, offset);
  if (module.version != kVm2Version) throw std::runtime_error("vm2: unsupported version");
  if (offset + code_size > bytes.size()) throw std::runtime_error("vm2: truncated code");
  module.code.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                     bytes.begin() + static_cast<std::ptrdiff_t>(offset + code_size));
  offset += code_size;
  const auto const_pool_size = read_u32(bytes, offset);
  if (offset + const_pool_size + kVm2KeyContextIdSize > bytes.size()) throw std::runtime_error("vm2: truncated const pool");
  if ((const_pool_size % 16u) != 0) throw std::runtime_error("vm2: const pool must be 16-byte aligned");
  module.const_pool.resize(const_pool_size / 16u);
  for (std::size_t i = 0; i < module.const_pool.size(); ++i) {
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset + i * 16u), 16, module.const_pool[i].bytes.begin());
  }
  offset += const_pool_size;
  std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), kVm2KeyContextIdSize, module.key_context_id.begin());
  if (module.entry_pc > module.code.size()) throw std::runtime_error("vm2: entry_pc out of range");
  module.runtime_id = g_next_vm2_module_id.fetch_add(1);
  module.function_entries = collect_function_entries(module.code, module.entry_pc);
  return module;
}

std::vector<std::uint8_t> Vm2Module::serialize() const {
  std::vector<std::uint8_t> out;
  out.insert(out.end(), kVm2Magic.begin(), kVm2Magic.end());
  append_u16(out, version);
  append_u16(out, module_flags);
  append_u32(out, entry_pc);
  append_u32(out, static_cast<std::uint32_t>(code.size()));
  out.insert(out.end(), code.begin(), code.end());
  append_u32(out, static_cast<std::uint32_t>(const_pool.size() * 16u));
  for (const auto& entry : const_pool) out.insert(out.end(), entry.bytes.begin(), entry.bytes.end());
  out.insert(out.end(), key_context_id.begin(), key_context_id.end());
  return out;
}

void Vm2Module::save_to_file(const std::string& path) const {
  const auto bytes = serialize();
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("vm2: failed to create module '" + path + "'");
  output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

Vm2Module assemble_module_text(std::string_view text, std::uint16_t module_flags) {
  const auto program = parse_assembly(text);
  Vm2Module module;
  module.module_flags = module_flags;
  module.const_pool = program.const_pool;
  module.key_context_id = program.key_context_id;
  for (const auto& inst : program.instructions) {
    const auto opcode = parse_opcode(inst.op);
    append_u16(module.code, static_cast<std::uint16_t>(opcode));
    switch (opcode) {
      case Opcode::nop:
      case Opcode::brk:
      case Opcode::bret:
      case Opcode::pret:
      case Opcode::xret:
        break;
      case Opcode::ftrap:
        append_u32(module.code, static_cast<std::uint32_t>(parse_u64_value(inst.operands.at(0))));
        break;
      case Opcode::ildimm:
        module.code.push_back(parse_general_register(inst.operands.at(0)));
        append_u64(module.code, static_cast<std::uint64_t>(parse_i64(inst.operands.at(1))));
        break;
      case Opcode::vldimm:
        module.code.push_back(parse_vector_register(inst.operands.at(0)));
        append_u32(module.code, resolve_const(inst.operands.at(1), program.const_labels));
        break;
      case Opcode::imov:
      case Opcode::ineg:
      case Opcode::inot:
        module.code.push_back(parse_general_register(inst.operands.at(0)));
        module.code.push_back(parse_general_register(inst.operands.at(1)));
        break;
      case Opcode::iadd:
      case Opcode::isub:
      case Opcode::imul:
      case Opcode::idiv:
      case Opcode::imod:
      case Opcode::iand:
      case Opcode::ior:
      case Opcode::ixor:
      case Opcode::ishl:
      case Opcode::ishr:
      case Opcode::isar:
        module.code.push_back(parse_general_register(inst.operands.at(0)));
        module.code.push_back(parse_general_register(inst.operands.at(1)));
        module.code.push_back(parse_general_register(inst.operands.at(2)));
        break;
      case Opcode::vadd128:
      case Opcode::vsub128:
      case Opcode::vmul128:
      case Opcode::vxor128:
        module.code.push_back(parse_vector_register(inst.operands.at(0)));
        module.code.push_back(parse_vector_register(inst.operands.at(1)));
        module.code.push_back(parse_vector_register(inst.operands.at(2)));
        break;
      case Opcode::imemld8:
      case Opcode::imemld16:
      case Opcode::imemld32:
      case Opcode::imemld64: {
        module.code.push_back(parse_general_register(inst.operands.at(0)));
        const auto mem = parse_memory_operand(inst.operands.at(1));
        module.code.push_back(mem.base);
        append_i32(module.code, mem.offset);
        break;
      }
      case Opcode::imemst8:
      case Opcode::imemst16:
      case Opcode::imemst32:
      case Opcode::imemst64: {
        const auto mem = parse_memory_operand(inst.operands.at(0));
        module.code.push_back(mem.base);
        module.code.push_back(parse_general_register(inst.operands.at(1)));
        append_i32(module.code, mem.offset);
        break;
      }
      case Opcode::vmemld128: {
        module.code.push_back(parse_vector_register(inst.operands.at(0)));
        const auto mem = parse_memory_operand(inst.operands.at(1));
        module.code.push_back(mem.base);
        append_i32(module.code, mem.offset);
        break;
      }
      case Opcode::vmemst128: {
        const auto mem = parse_memory_operand(inst.operands.at(0));
        module.code.push_back(mem.base);
        module.code.push_back(parse_vector_register(inst.operands.at(1)));
        append_i32(module.code, mem.offset);
        break;
      }
      case Opcode::jmp:
        append_u32(module.code, resolve_target(inst.operands.at(0), program.labels));
        break;
      case Opcode::jp:
      case Opcode::jnp:
        module.code.push_back(parse_predicate(inst.operands.at(0)));
        append_u32(module.code, resolve_target(inst.operands.at(1), program.labels));
        break;
      case Opcode::blnk:
        append_u32(module.code, resolve_target(inst.operands.at(0), program.labels));
        module.code.push_back(static_cast<std::uint8_t>(inst.operands.size() > 1 ? parse_u64_value(inst.operands.at(1)) : 0u));
        break;
      case Opcode::pcall:
        module.code.push_back(parse_predicate(inst.operands.at(0)));
        append_u32(module.code, resolve_target(inst.operands.at(1), program.labels));
        module.code.push_back(static_cast<std::uint8_t>(inst.operands.size() > 2 ? parse_u64_value(inst.operands.at(2)) : 0u));
        break;
      case Opcode::xcall:
        module.code.push_back(parse_domain_token(inst.operands.at(0)));
        append_u32(module.code, static_cast<std::uint32_t>(parse_u64_value(inst.operands.at(1))));
        module.code.push_back(static_cast<std::uint8_t>(parse_u64_value(inst.operands.at(2))));
        module.code.push_back(static_cast<std::uint8_t>(inst.operands.size() > 3 ? parse_u64_value(inst.operands.at(3)) : 0u));
        module.code.push_back(static_cast<std::uint8_t>(inst.operands.size() > 4 ? parse_u64_value(inst.operands.at(4)) : 0u));
        break;
      case Opcode::tsload:
        module.code.push_back(parse_general_register(inst.operands.at(0)));
        append_u32(module.code, static_cast<std::uint32_t>(parse_u64_value(inst.operands.at(1))));
        break;
      case Opcode::tsrelease:
        module.code.push_back(parse_general_register(inst.operands.at(0)));
        break;
    }
  }
  if (auto it = program.labels.find("entry"); it != program.labels.end()) {
    module.entry_pc = it->second;
  } else {
    module.entry_pc = 0;
  }
  module.runtime_id = g_next_vm2_module_id.fetch_add(1);
  module.function_entries = collect_function_entries(module.code, module.entry_pc);
  return module;
}

std::string opcode_name(Opcode opcode) {
  switch (opcode) {
    case Opcode::nop: return "nop";
    case Opcode::brk: return "brk";
    case Opcode::ftrap: return "ftrap";
    case Opcode::ildimm: return "ildimm";
    case Opcode::vldimm: return "vldimm";
    case Opcode::imov: return "imov";
    case Opcode::iadd: return "iadd";
    case Opcode::isub: return "isub";
    case Opcode::imul: return "imul";
    case Opcode::idiv: return "idiv";
    case Opcode::imod: return "imod";
    case Opcode::iand: return "iand";
    case Opcode::ior: return "ior";
    case Opcode::ixor: return "ixor";
    case Opcode::ishl: return "ishl";
    case Opcode::ishr: return "ishr";
    case Opcode::isar: return "isar";
    case Opcode::ineg: return "ineg";
    case Opcode::inot: return "inot";
    case Opcode::vadd128: return "vadd128";
    case Opcode::vsub128: return "vsub128";
    case Opcode::vmul128: return "vmul128";
    case Opcode::vxor128: return "vxor128";
    case Opcode::imemld8: return "imemld8";
    case Opcode::imemld16: return "imemld16";
    case Opcode::imemld32: return "imemld32";
    case Opcode::imemld64: return "imemld64";
    case Opcode::imemst8: return "imemst8";
    case Opcode::imemst16: return "imemst16";
    case Opcode::imemst32: return "imemst32";
    case Opcode::imemst64: return "imemst64";
    case Opcode::vmemld128: return "vmemld128";
    case Opcode::vmemst128: return "vmemst128";
    case Opcode::jmp: return "jmp";
    case Opcode::jp: return "jp";
    case Opcode::jnp: return "jnp";
    case Opcode::blnk: return "blnk";
    case Opcode::bret: return "bret";
    case Opcode::pcall: return "pcall";
    case Opcode::pret: return "pret";
    case Opcode::xcall: return "xcall";
    case Opcode::xret: return "xret";
    case Opcode::tsload: return "tsload";
    case Opcode::tsrelease: return "tsrelease";
  }
  return "unknown";
}

std::string disassemble_module(const Vm2Module& module) {
  std::ostringstream out;
  if (module.key_context_id != std::array<std::uint8_t, kVm2KeyContextIdSize>{}) {
    out << ".keyctx 0x";
    for (const auto byte : module.key_context_id) {
      out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
    }
    out << std::dec << "\n";
  }
  for (std::size_t i = 0; i < module.const_pool.size(); ++i) {
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;
    for (int j = 0; j < 8; ++j) lo |= static_cast<std::uint64_t>(module.const_pool[i].bytes[static_cast<std::size_t>(j)]) << (8 * j);
    for (int j = 0; j < 8; ++j) hi |= static_cast<std::uint64_t>(module.const_pool[i].bytes[8 + static_cast<std::size_t>(j)]) << (8 * j);
    out << ".vconst c" << i << ", 0x" << std::hex << lo << ", 0x" << hi << std::dec << "\n";
  }

  std::map<std::uint32_t, std::string> labels;

  for (std::size_t pc = 0; pc < module.code.size();) {
    const auto start = static_cast<std::uint32_t>(pc);
    const auto label_it = labels.find(start);
    if (label_it != labels.end()) out << label_it->second << ":\n";
    const auto opcode = static_cast<Opcode>(read_u16(module.code, pc));
    out << "  " << opcode_name(opcode);
    switch (opcode) {
      case Opcode::nop:
      case Opcode::brk:
      case Opcode::bret:
      case Opcode::pret:
      case Opcode::xret:
        break;
      case Opcode::ftrap:
        out << ' ' << read_u32(module.code, pc);
        break;
      case Opcode::ildimm:
        out << " r" << static_cast<unsigned>(module.code.at(pc++)) << ", "
            << static_cast<std::int64_t>(read_u64(module.code, pc));
        break;
      case Opcode::vldimm:
        out << " q" << static_cast<unsigned>(module.code.at(pc++)) << ", c" << read_u32(module.code, pc);
        break;
      case Opcode::imov:
      case Opcode::ineg:
      case Opcode::inot:
        out << " r" << static_cast<unsigned>(module.code.at(pc++)) << ", r" << static_cast<unsigned>(module.code.at(pc++));
        break;
      case Opcode::iadd:
      case Opcode::isub:
      case Opcode::imul:
      case Opcode::idiv:
      case Opcode::imod:
      case Opcode::iand:
      case Opcode::ior:
      case Opcode::ixor:
      case Opcode::ishl:
      case Opcode::ishr:
      case Opcode::isar:
        out << " r" << static_cast<unsigned>(module.code.at(pc++)) << ", r" << static_cast<unsigned>(module.code.at(pc++))
            << ", r" << static_cast<unsigned>(module.code.at(pc++));
        break;
      case Opcode::vadd128:
      case Opcode::vsub128:
      case Opcode::vmul128:
      case Opcode::vxor128:
        out << " q" << static_cast<unsigned>(module.code.at(pc++)) << ", q" << static_cast<unsigned>(module.code.at(pc++))
            << ", q" << static_cast<unsigned>(module.code.at(pc++));
        break;
      case Opcode::imemld8:
      case Opcode::imemld16:
      case Opcode::imemld32:
      case Opcode::imemld64: {
        const auto dst = module.code.at(pc++);
        const auto base = module.code.at(pc++);
        const auto offset = read_i32(module.code, pc);
        out << " r" << static_cast<unsigned>(dst) << ", " << memory_operand_text(base, offset);
        break;
      }
      case Opcode::imemst8:
      case Opcode::imemst16:
      case Opcode::imemst32:
      case Opcode::imemst64: {
        const auto base = module.code.at(pc++);
        const auto src = module.code.at(pc++);
        const auto offset = read_i32(module.code, pc);
        out << ' ' << memory_operand_text(base, offset) << ", r" << static_cast<unsigned>(src);
        break;
      }
      case Opcode::vmemld128: {
        const auto dst = module.code.at(pc++);
        const auto base = module.code.at(pc++);
        const auto offset = read_i32(module.code, pc);
        out << " q" << static_cast<unsigned>(dst) << ", " << memory_operand_text(base, offset);
        break;
      }
      case Opcode::vmemst128: {
        const auto base = module.code.at(pc++);
        const auto src = module.code.at(pc++);
        const auto offset = read_i32(module.code, pc);
        out << ' ' << memory_operand_text(base, offset) << ", q" << static_cast<unsigned>(src);
        break;
      }
      case Opcode::jmp:
        out << ' ' << read_u32(module.code, pc);
        break;
      case Opcode::jp:
      case Opcode::jnp:
        out << " p" << static_cast<unsigned>(module.code.at(pc++)) << ", " << read_u32(module.code, pc);
        break;
      case Opcode::blnk:
        out << ' ' << read_u32(module.code, pc) << ", " << static_cast<unsigned>(module.code.at(pc++));
        break;
      case Opcode::pcall:
        out << " p" << static_cast<unsigned>(module.code.at(pc++)) << ", " << read_u32(module.code, pc)
            << ", " << static_cast<unsigned>(module.code.at(pc++));
        break;
      case Opcode::xcall:
        out << ' ' << static_cast<unsigned>(module.code.at(pc++)) << ", " << read_u32(module.code, pc)
            << ", " << static_cast<unsigned>(module.code.at(pc++))
            << ", " << static_cast<unsigned>(module.code.at(pc++))
            << ", " << static_cast<unsigned>(module.code.at(pc++));
        break;
      case Opcode::tsload:
        out << " r" << static_cast<unsigned>(module.code.at(pc++)) << ", " << read_u32(module.code, pc);
        break;
    }
    out << "\n";
  }
  return out.str();
}

const void* handler_table_identity() noexcept { return &kVm2HandlerTableIdentity; }

const char* Facade::status() const noexcept { return "vm2_ready"; }

}  // namespace vmp::runtime::vm2
