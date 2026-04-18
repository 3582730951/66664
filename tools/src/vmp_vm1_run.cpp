#include <cstdlib>
#include <iostream>
#include <string>

#include <vmp/runtime/audit/audit.h>
#include <vmp/runtime/audit/reaction.h>
#include <vmp/runtime/vm1/vm1.h>

int main(int argc, char** argv) {
  try {
    int argi = 1;
    std::string audit_path;
    if (argi < argc && std::string(argv[argi]) == "--audit-path") {
      if (argi + 1 >= argc) {
        std::cerr << "--audit-path requires a value\n";
        return 2;
      }
      audit_path = argv[argi + 1];
      argi += 2;
    }
    if (argi >= argc) {
      std::cerr << "usage: vmp-vm1-run [--audit-path <log>] <module.vm1> [args...]\n";
      return 2;
    }
    const auto module = vmp::runtime::vm1::Vm1Module::load_from_file(argv[argi++]);
    vmp::runtime::vm1::Vm1Context context(module);
    for (int i = 0; argi < argc && i < 8; ++i, ++argi) {
      context.vr[static_cast<std::size_t>(i)] = std::strtoull(argv[argi], nullptr, 0);
    }

    std::unique_ptr<vmp::runtime::audit::AuditWriter> writer;
    std::unique_ptr<vmp::runtime::audit::ReactionDispatcher> dispatcher;
    if (!audit_path.empty()) {
      writer = std::make_unique<vmp::runtime::audit::AuditWriter>(audit_path);
      dispatcher = std::make_unique<vmp::runtime::audit::ReactionDispatcher>(*writer,
                                                                             vmp::runtime::audit::ReactionPolicy::audit_only);
      context.audit_dispatcher = dispatcher.get();
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
