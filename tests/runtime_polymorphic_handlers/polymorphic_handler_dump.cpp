#include "test_common.h"

#include <iostream>
#include <nlohmann/json.hpp>

#include <vmp/runtime/vm1/vm1.h>
#include <vmp/runtime/vm2/vm2.h>

namespace {

nlohmann::json dump_vm1() {
  const auto& layout = vmp::runtime::vm1::polymorphic_handler_layout();
  nlohmann::json out;
  out["seed"] = layout.build_seed;
  out["layout_fingerprint"] = layout.layout_fingerprint;
  for (const auto& entry : layout.entries) {
    out["prefix_hashes"].push_back(vmp::tests::runtime_polymorphic_handlers::code_prefix_hash(entry.entry));
    out["variants"].push_back(entry.variant);
    out["junk_lengths"].push_back(entry.junk_length);
  }
  auto module = vmp::runtime::vm1::assemble_module_text("ldi_u64 vr0, 7\nldi_u64 vr1, 5\nadd vr0, vr0, vr1\nret\n");
  vmp::runtime::vm1::Vm1Context context(module);
  vmp::runtime::vm1::Vm1Interpreter interpreter;
  out["ret_int"] = interpreter.execute(context).ret_int;
  return out;
}

nlohmann::json dump_vm2() {
  const auto& layout = vmp::runtime::vm2::polymorphic_handler_layout();
  nlohmann::json out;
  out["seed"] = layout.build_seed;
  out["layout_fingerprint"] = layout.layout_fingerprint;
  for (const auto& entry : layout.entries) {
    out["prefix_hashes"].push_back(vmp::tests::runtime_polymorphic_handlers::code_prefix_hash(entry.entry));
    out["variants"].push_back(entry.variant);
    out["junk_lengths"].push_back(entry.junk_length);
  }
  auto module = vmp::runtime::vm2::assemble_module_text("ildimm r0, 7\nildimm r1, 5\niadd r0, r0, r1\nbret\n");
  vmp::runtime::vm2::Vm2Context context(module);
  vmp::runtime::vm2::Vm2Interpreter interpreter;
  out["ret_int"] = interpreter.execute(context).ret_int;
  return out;
}

}  // namespace

int main() {
  nlohmann::json out;
  out["vm1"] = dump_vm1();
  out["vm2"] = dump_vm2();
  std::cout << out.dump() << std::endl;
  return 0;
}
