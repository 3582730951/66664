#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include <vmp/runtime/audit/audit.h>
#include <vmp/runtime/audit/reaction.h>
#include <vmp/runtime/bridge/bridge.h>
#include <vmp/runtime/strings/cipher.h>
#include <vmp/runtime/strings/keyctx.h>
#include <vmp/runtime/vm1/vm1.h>

#include "string_protect_common.h"

namespace {

struct Options {
  std::string audit_path;
  std::string string_pool_path;
  std::string string_idx_path;
  std::string key_env = "VMP_STRING_MASTER_KEY";
  std::uint32_t native_print_string = 0;
  std::string module_path;
  std::vector<std::string> args;
};

Options parse_args(int argc, char** argv) {
  Options options;
  for (int argi = 1; argi < argc; ++argi) {
    const std::string arg = argv[argi];
    if (arg == "--audit-path") {
      options.audit_path = argv[++argi];
    } else if (arg == "--string-pool") {
      options.string_pool_path = argv[++argi];
    } else if (arg == "--string-idx") {
      options.string_idx_path = argv[++argi];
    } else if (arg == "--key-env") {
      options.key_env = argv[++argi];
    } else if (arg == "--native-print-string") {
      options.native_print_string = static_cast<std::uint32_t>(std::stoul(argv[++argi]));
    } else if (arg.rfind("--", 0) == 0) {
      throw std::runtime_error("unknown argument: " + arg);
    } else if (options.module_path.empty()) {
      options.module_path = arg;
    } else {
      options.args.push_back(arg);
    }
  }
  if (options.module_path.empty()) {
    throw std::runtime_error("module path is required");
  }
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto options = parse_args(argc, argv);
    const auto module = vmp::runtime::vm1::Vm1Module::load_from_file(options.module_path);
    vmp::runtime::vm1::Vm1Context context(module);
    for (int i = 0; i < static_cast<int>(options.args.size()) && i < 8; ++i) {
      context.vr[static_cast<std::size_t>(i)] = std::strtoull(options.args[static_cast<std::size_t>(i)].c_str(), nullptr, 0);
    }

    std::unique_ptr<vmp::runtime::audit::AuditWriter> writer;
    std::unique_ptr<vmp::runtime::audit::ReactionDispatcher> dispatcher;
    if (!options.audit_path.empty()) {
      writer = std::make_unique<vmp::runtime::audit::AuditWriter>(options.audit_path);
      dispatcher = std::make_unique<vmp::runtime::audit::ReactionDispatcher>(*writer,
                                                                             vmp::runtime::audit::ReactionPolicy::audit_only);
      context.audit_dispatcher = dispatcher.get();
    }

    if (!options.string_pool_path.empty() || !options.string_idx_path.empty()) {
      if (options.string_pool_path.empty() || options.string_idx_path.empty()) {
        throw std::runtime_error("--string-pool and --string-idx must be used together");
      }
      const auto [index, salt] = vmp::tools::strings_tool::load_index_file(options.string_idx_path);
      auto key = vmp::runtime::strings::KeyContext(vmp::tools::strings_tool::key_from_env(options.key_env), salt);
      context.string_pool = std::make_shared<vmp::runtime::strings::StringPool>(
          vmp::tools::strings_tool::read_binary(options.string_pool_path), index, std::move(key));
      context.string_pool->set_audit_dispatcher(context.audit_dispatcher);
    }

    vmp::runtime::bridge::BridgeRegistry registry;
    if (options.native_print_string != 0) {
      registry.register_native(options.native_print_string, [&](const vmp::runtime::bridge::DomainCallArgs& args) {
        const auto text = context.transient_string(args.ints.at(0));
        std::cout << "native_string=" << text << '\n';
        return vmp::runtime::bridge::DomainCallResult{static_cast<std::uint64_t>(text.size()), 0.0, 0};
      });
      context.bridge_registry = &registry;
    }

    vmp::runtime::vm1::Vm1Interpreter interpreter;
    const auto result = interpreter.execute(context);
    if (writer) {
      writer->flush();
    }
    std::cout << "ret_int=" << result.ret_int << " ret_float=" << result.ret_float << '\n';
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "vmp-vm1-run failed: " << ex.what() << '\n';
    return 1;
  }
}
