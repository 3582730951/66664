#include <vmp/runtime/vm1/vm1.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace vmp::runtime::vm1 {
namespace {

std::uint16_t read_u16(const std::vector<std::uint8_t>& code, std::size_t& pc) {
  if (pc + 2 > code.size()) {
    throw VmException(VmTrapCode::invalid_module, static_cast<std::uint32_t>(pc), "vm1: truncated u16 operand");
  }
  std::uint16_t value = static_cast<std::uint16_t>(code[pc]) |
                        static_cast<std::uint16_t>(code[pc + 1] << 8u);
  pc += 2;
  return value;
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& code, std::size_t& pc) {
  if (pc + 4 > code.size()) {
    throw VmException(VmTrapCode::invalid_module, static_cast<std::uint32_t>(pc), "vm1: truncated u32 operand");
  }
  std::uint32_t value = 0;
  for (int i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(code[pc + static_cast<std::size_t>(i)]) << (8 * i);
  }
  pc += 4;
  return value;
}

std::uint64_t read_u64(const std::vector<std::uint8_t>& code, std::size_t& pc) {
  if (pc + 8 > code.size()) {
    throw VmException(VmTrapCode::invalid_module, static_cast<std::uint32_t>(pc), "vm1: truncated u64 operand");
  }
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(code[pc + static_cast<std::size_t>(i)]) << (8 * i);
  }
  pc += 8;
  return value;
}

std::int32_t read_i32(const std::vector<std::uint8_t>& code, std::size_t& pc) {
  return static_cast<std::int32_t>(read_u32(code, pc));
}

double bit_cast_double(std::uint64_t value) {
  double out = 0.0;
  std::memcpy(&out, &value, sizeof(out));
  return out;
}

std::uint64_t flags_carry_add(std::uint64_t lhs, std::uint64_t rhs) { return lhs > std::numeric_limits<std::uint64_t>::max() - rhs; }

bool flags_overflow_add(std::int64_t lhs, std::int64_t rhs, std::int64_t result) {
  return ((lhs ^ result) & (rhs ^ result)) < 0;
}

bool flags_overflow_sub(std::int64_t lhs, std::int64_t rhs, std::int64_t result) {
  return ((lhs ^ rhs) & (lhs ^ result)) < 0;
}

void set_common_flags(Vm1Context& context, std::uint64_t value) {
  context.flags.zero = (value == 0);
  context.flags.neg = (static_cast<std::int64_t>(value) < 0);
}

void set_logic_flags(Vm1Context& context, std::uint64_t value) {
  set_common_flags(context, value);
  context.flags.carry = false;
  context.flags.overflow = false;
}

void dispatch_audit(Vm1Context& context, const std::string& event_type, const std::string& note) {
  if (context.audit_dispatcher == nullptr) {
    return;
  }
  context.audit_dispatcher->dispatch(
      vmp::runtime::audit::make_event(event_type, note, context.pc, "vm1", "", 0),
      vmp::runtime::audit::ReactionPolicy::audit_only);
}

[[noreturn]] void raise_trap(Vm1Context& context, VmTrapCode code, const std::string& message,
                             const char* event_type = nullptr) {
  if (event_type != nullptr) {
    dispatch_audit(context, event_type, message);
  }
  throw VmException(code, context.pc, message);
}

std::uint64_t resolve_address(const Vm1Context& context, std::uint8_t base, std::int32_t offset) {
  std::uint64_t base_value = 0;
  if (base == static_cast<std::uint8_t>(MemoryBase::stack_pointer)) {
    base_value = context.sp;
  } else if (base < kVm1GeneralRegisterCount) {
    base_value = context.vr[base];
  } else {
    throw VmException(VmTrapCode::invalid_register, context.pc, "vm1: invalid memory base register");
  }
  if (offset >= 0) {
    return base_value + static_cast<std::uint64_t>(offset);
  }
  return base_value - static_cast<std::uint64_t>(-static_cast<std::int64_t>(offset));
}

void compare_and_set_flags(Vm1Context& context, std::uint64_t lhs, std::uint64_t rhs) {
  const auto s_lhs = static_cast<std::int64_t>(lhs);
  const auto s_rhs = static_cast<std::int64_t>(rhs);
  const auto diff = static_cast<std::uint64_t>(lhs - rhs);
  set_common_flags(context, diff);
  context.flags.carry = lhs < rhs;
  context.flags.overflow = flags_overflow_sub(s_lhs, s_rhs, static_cast<std::int64_t>(diff));
}

void execute_call(Vm1Context& context, std::uint32_t target_pc, std::uint8_t arg_count, std::uint32_t return_pc) {
  const std::size_t spill_count = arg_count > 8 ? static_cast<std::size_t>(arg_count - 8) : 0u;
  const std::uint64_t new_sp = context.stack_top_;
  const std::uint64_t new_top = new_sp + static_cast<std::uint64_t>(spill_count * sizeof(std::uint64_t));
  if (new_top > context.stack_size()) {
    raise_trap(context, VmTrapCode::stack_overflow, "vm1: stack overflow during call", "vm1_stack_overflow");
  }
  for (std::size_t i = 0; i < spill_count; ++i) {
    context.write_memory<std::uint64_t>(new_sp + static_cast<std::uint64_t>(i * sizeof(std::uint64_t)),
                                        context.vr[8 + i]);
  }
  Vm1Context::CallFrame frame;
  frame.vr = context.vr;
  frame.vfr = context.vfr;
  frame.flags = context.flags;
  frame.return_pc = return_pc;
  frame.caller_sp = context.sp;
  frame.caller_stack_top = context.stack_top_;
  frame.arg_count = arg_count;
  context.frames_.push_back(frame);
  context.sp = new_sp;
  context.stack_top_ = new_top;
  context.pc = target_pc;
}

void execute_return(Vm1Context& context, bool explicit_domain_ret, bool& halted) {
  (void)explicit_domain_ret;
  const auto ret_int = context.vr[0];
  const auto ret_float = context.vfr[0];
  if (context.frames_.empty()) {
    context.clear_frame_transient_strings();
    halted = true;
    context.vr[0] = ret_int;
    context.vfr[0] = ret_float;
    return;
  }
  context.clear_frame_transient_strings();
  const auto frame = context.frames_.back();
  context.frames_.pop_back();
  context.vr = frame.vr;
  context.vfr = frame.vfr;
  context.flags = frame.flags;
  context.pc = frame.return_pc;
  context.sp = frame.caller_sp;
  context.stack_top_ = frame.caller_stack_top;
  context.vr[0] = ret_int;
  context.vfr[0] = ret_float;
  set_logic_flags(context, ret_int);
}

vmp::runtime::bridge::Domain bridge_domain_from_byte(std::uint8_t raw) {
  switch (raw) {
    case 0: return vmp::runtime::bridge::Domain::native;
    case 1: return vmp::runtime::bridge::Domain::vm1;
    case 2: return vmp::runtime::bridge::Domain::vm2;
    default: throw std::runtime_error("vm1: invalid domain byte");
  }
}

}  // namespace

ExecutionResult Vm1Interpreter::execute(Vm1Context& context) {
  if (context.module == nullptr) {
    throw VmException(VmTrapCode::invalid_module, 0, "vm1: null module");
  }
  if (context.pc > context.module->code.size()) {
    throw VmException(VmTrapCode::invalid_module, context.pc, "vm1: entry pc out of range");
  }
  try {
  bool halted = false;
  while (!halted) {
    bool control_flow_changed = false;
    if (context.pc >= context.module->code.size()) {
      throw VmException(VmTrapCode::invalid_module, context.pc, "vm1: pc out of range");
    }
    const std::uint32_t instruction_pc = context.pc;
    std::size_t cursor = context.pc;
    const auto opcode = static_cast<Opcode>(context.module->code[cursor++]);
    context.pc = static_cast<std::uint32_t>(cursor);
    switch (opcode) {
      case Opcode::nop:
        break;
      case Opcode::breakpoint:
        dispatch_audit(context, "vm1_breakpoint", "breakpoint opcode executed");
        break;
      case Opcode::trap: {
        const auto code = read_u32(context.module->code, cursor);
        context.pc = static_cast<std::uint32_t>(cursor);
        std::ostringstream oss;
        oss << "vm1: trap opcode status=" << code;
        raise_trap(context, VmTrapCode::trap_instruction, oss.str(), "vm1_trap");
      }
      case Opcode::ldi64: {
        const auto dst = context.module->code.at(cursor++);
        const auto imm = read_u64(context.module->code, cursor);
        context.vr.at(dst) = imm;
        set_logic_flags(context, imm);
        break;
      }
      case Opcode::ldi_u64: {
        const auto dst = context.module->code.at(cursor++);
        const auto imm = read_u64(context.module->code, cursor);
        context.vr.at(dst) = imm;
        set_logic_flags(context, imm);
        break;
      }
      case Opcode::ldi_f64: {
        const auto dst = context.module->code.at(cursor++);
        const auto imm = bit_cast_double(read_u64(context.module->code, cursor));
        if (dst >= kVm1FloatRegisterCount) {
          raise_trap(context, VmTrapCode::invalid_register, "vm1: float register out of range");
        }
        context.vfr[dst] = imm;
        context.flags.zero = (imm == 0.0);
        context.flags.neg = std::signbit(imm);
        context.flags.carry = false;
        context.flags.overflow = false;
        break;
      }
      case Opcode::mov: {
        const auto dst = context.module->code.at(cursor++);
        const auto src = context.module->code.at(cursor++);
        context.vr.at(dst) = context.vr.at(src);
        set_logic_flags(context, context.vr.at(dst));
        break;
      }
      case Opcode::add:
      case Opcode::sub:
      case Opcode::mul:
      case Opcode::div:
      case Opcode::mod:
      case Opcode::bit_and:
      case Opcode::bit_or:
      case Opcode::bit_xor:
      case Opcode::shl:
      case Opcode::shr:
      case Opcode::sar: {
        const auto dst = context.module->code.at(cursor++);
        const auto lhs_reg = context.module->code.at(cursor++);
        const auto rhs_reg = context.module->code.at(cursor++);
        const auto lhs = context.vr.at(lhs_reg);
        const auto rhs = context.vr.at(rhs_reg);
        std::uint64_t result = 0;
        switch (opcode) {
          case Opcode::add: {
            result = lhs + rhs;
            set_common_flags(context, result);
            context.flags.carry = flags_carry_add(lhs, rhs);
            context.flags.overflow = flags_overflow_add(static_cast<std::int64_t>(lhs), static_cast<std::int64_t>(rhs),
                                                        static_cast<std::int64_t>(result));
            break;
          }
          case Opcode::sub: {
            result = lhs - rhs;
            set_common_flags(context, result);
            context.flags.carry = lhs < rhs;
            context.flags.overflow = flags_overflow_sub(static_cast<std::int64_t>(lhs), static_cast<std::int64_t>(rhs),
                                                        static_cast<std::int64_t>(result));
            break;
          }
          case Opcode::mul: {
            result = lhs * rhs;
            set_common_flags(context, result);
            context.flags.carry = false;
            context.flags.overflow = false;
            break;
          }
          case Opcode::div: {
            if (rhs == 0) {
              raise_trap(context, VmTrapCode::divide_by_zero, "vm1: divide by zero");
            }
            result = static_cast<std::uint64_t>(static_cast<std::int64_t>(lhs) / static_cast<std::int64_t>(rhs));
            set_logic_flags(context, result);
            break;
          }
          case Opcode::mod: {
            if (rhs == 0) {
              raise_trap(context, VmTrapCode::divide_by_zero, "vm1: modulo by zero");
            }
            result = static_cast<std::uint64_t>(static_cast<std::int64_t>(lhs) % static_cast<std::int64_t>(rhs));
            set_logic_flags(context, result);
            break;
          }
          case Opcode::bit_and:
            result = lhs & rhs;
            set_logic_flags(context, result);
            break;
          case Opcode::bit_or:
            result = lhs | rhs;
            set_logic_flags(context, result);
            break;
          case Opcode::bit_xor:
            result = lhs ^ rhs;
            set_logic_flags(context, result);
            break;
          case Opcode::shl:
            result = lhs << (rhs & 63u);
            set_logic_flags(context, result);
            break;
          case Opcode::shr:
            result = lhs >> (rhs & 63u);
            set_logic_flags(context, result);
            break;
          case Opcode::sar:
            result = static_cast<std::uint64_t>(static_cast<std::int64_t>(lhs) >> (rhs & 63u));
            set_logic_flags(context, result);
            break;
          default:
            break;
        }
        context.vr.at(dst) = result;
        break;
      }
      case Opcode::neg:
      case Opcode::bit_not: {
        const auto dst = context.module->code.at(cursor++);
        const auto src = context.module->code.at(cursor++);
        std::uint64_t result = 0;
        if (opcode == Opcode::neg) {
          result = static_cast<std::uint64_t>(-static_cast<std::int64_t>(context.vr.at(src)));
        } else {
          result = ~context.vr.at(src);
        }
        context.vr.at(dst) = result;
        set_logic_flags(context, result);
        break;
      }
      case Opcode::load_mem8:
      case Opcode::load_mem16:
      case Opcode::load_mem32:
      case Opcode::load_mem64: {
        const auto dst = context.module->code.at(cursor++);
        const auto base = context.module->code.at(cursor++);
        const auto offset = read_i32(context.module->code, cursor);
        const auto address = resolve_address(context, base, offset);
        switch (opcode) {
          case Opcode::load_mem8: context.vr.at(dst) = context.read_memory<std::uint8_t>(address); break;
          case Opcode::load_mem16: context.vr.at(dst) = context.read_memory<std::uint16_t>(address); break;
          case Opcode::load_mem32: context.vr.at(dst) = context.read_memory<std::uint32_t>(address); break;
          case Opcode::load_mem64: context.vr.at(dst) = context.read_memory<std::uint64_t>(address); break;
          default: break;
        }
        set_logic_flags(context, context.vr.at(dst));
        break;
      }
      case Opcode::store_mem8:
      case Opcode::store_mem16:
      case Opcode::store_mem32:
      case Opcode::store_mem64: {
        const auto base = context.module->code.at(cursor++);
        const auto src = context.module->code.at(cursor++);
        const auto offset = read_i32(context.module->code, cursor);
        const auto address = resolve_address(context, base, offset);
        switch (opcode) {
          case Opcode::store_mem8: context.write_memory<std::uint8_t>(address, static_cast<std::uint8_t>(context.vr.at(src))); break;
          case Opcode::store_mem16: context.write_memory<std::uint16_t>(address, static_cast<std::uint16_t>(context.vr.at(src))); break;
          case Opcode::store_mem32: context.write_memory<std::uint32_t>(address, static_cast<std::uint32_t>(context.vr.at(src))); break;
          case Opcode::store_mem64: context.write_memory<std::uint64_t>(address, context.vr.at(src)); break;
          default: break;
        }
        break;
      }
      case Opcode::jmp: {
        const auto target = read_u32(context.module->code, cursor);
        context.pc = target;
        control_flow_changed = true;
        break;
      }
      case Opcode::jeq:
      case Opcode::jne:
      case Opcode::jlt:
      case Opcode::jle:
      case Opcode::jgt:
      case Opcode::jge: {
        const auto lhs_reg = context.module->code.at(cursor++);
        const auto rhs_reg = context.module->code.at(cursor++);
        const auto target = read_u32(context.module->code, cursor);
        const auto lhs = context.vr.at(lhs_reg);
        const auto rhs = context.vr.at(rhs_reg);
        compare_and_set_flags(context, lhs, rhs);
        bool take = false;
        switch (opcode) {
          case Opcode::jeq: take = lhs == rhs; break;
          case Opcode::jne: take = lhs != rhs; break;
          case Opcode::jlt: take = static_cast<std::int64_t>(lhs) < static_cast<std::int64_t>(rhs); break;
          case Opcode::jle: take = static_cast<std::int64_t>(lhs) <= static_cast<std::int64_t>(rhs); break;
          case Opcode::jgt: take = static_cast<std::int64_t>(lhs) > static_cast<std::int64_t>(rhs); break;
          case Opcode::jge: take = static_cast<std::int64_t>(lhs) >= static_cast<std::int64_t>(rhs); break;
          default: break;
        }
        context.pc = take ? target : static_cast<std::uint32_t>(cursor);
        control_flow_changed = true;
        break;
      }
      case Opcode::call: {
        const auto target = read_u32(context.module->code, cursor);
        const auto arg_count = context.module->code.at(cursor++);
        execute_call(context, target, arg_count, static_cast<std::uint32_t>(cursor));
        control_flow_changed = true;
        break;
      }
      case Opcode::ret:
        execute_return(context, false, halted);
        control_flow_changed = true;
        break;
      case Opcode::domain_call: {
        const auto domain = context.module->code.at(cursor++);
        const auto id = read_u32(context.module->code, cursor);
        const auto int_count = context.module->code.at(cursor++);
        const auto float_count = context.module->code.at(cursor++);
        const auto opaque_count = context.module->code.at(cursor++);
        context.pc = static_cast<std::uint32_t>(cursor);
        if (context.bridge_registry == nullptr) {
          raise_trap(context, VmTrapCode::bridge_error, "vm1: bridge registry not configured");
        }
        vmp::runtime::bridge::DomainCallArgs args;
        args.ints.reserve(int_count);
        for (std::uint8_t i = 0; i < int_count; ++i) {
          if (i < 8) {
            args.ints.push_back(context.vr[i]);
          } else {
            args.ints.push_back(context.read_memory<std::uint64_t>(context.sp + static_cast<std::uint64_t>((i - 8) * 8)));
          }
        }
        args.floats.reserve(float_count);
        for (std::uint8_t i = 0; i < float_count; ++i) {
          args.floats.push_back(i < kVm1FloatRegisterCount ? context.vfr[i] : 0.0);
        }
        args.opaque.reserve(opaque_count);
        for (std::uint8_t i = 0; i < opaque_count; ++i) {
          args.opaque.push_back(reinterpret_cast<void*>(static_cast<std::uintptr_t>(context.vr[i])));
        }
        try {
          const auto result = context.bridge_registry->call(bridge_domain_from_byte(domain), id, args, context.max_bridge_depth);
          context.vr[0] = result.ret_int;
          context.vfr[0] = result.ret_float;
          context.vr[31] = static_cast<std::uint64_t>(static_cast<std::int64_t>(result.status));
          set_logic_flags(context, result.ret_int);
        } catch (const vmp::runtime::bridge::BridgeException& ex) {
          raise_trap(context, VmTrapCode::bridge_error, ex.what());
        }
        break;
      }
      case Opcode::domain_ret:
        execute_return(context, true, halted);
        control_flow_changed = true;
        break;
      case Opcode::load_transient_string: {
        const auto dst = context.module->code.at(cursor++);
        const auto id = read_u32(context.module->code, cursor);
        // JIT hook: future JIT paths must not constant-propagate decrypted transient bytes into caches.
        context.vr.at(dst) = context.materialize_transient_string(id);
        set_logic_flags(context, context.vr.at(dst));
        break;
      }
      case Opcode::release_transient_string: {
        const auto src = context.module->code.at(cursor++);
        context.release_transient_string(context.vr.at(src));
        context.vr.at(src) = 0;
        set_logic_flags(context, 0);
        break;
      }
      default:
        context.pc = instruction_pc;
        raise_trap(context, VmTrapCode::unknown_opcode, "vm1: unknown opcode", "vm1_unknown_opcode");
    }
    if (!halted && !control_flow_changed) {
      context.pc = static_cast<std::uint32_t>(cursor);
    }
  }
  return ExecutionResult{context.vr[0], context.vfr[0]};
  } catch (...) {
    context.clear_all_transient_strings();
    throw;
  }
}

}  // namespace vmp::runtime::vm1
