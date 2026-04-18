#pragma once

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace vmp::tools {

struct DriverConfig {
  std::string host_compiler;
  std::string fallback_tool_path;
  std::string collector_tool_path;
  std::string plugin_path;
};

struct ParsedArgs {
  std::vector<std::string> compiler_args;
  std::vector<std::string> source_files;
  std::string collect_path;
};

inline bool looks_like_source_file(const std::string& arg) {
  const auto ext = std::filesystem::path(arg).extension().string();
  return ext == ".c" || ext == ".cc" || ext == ".cp" || ext == ".cpp" || ext == ".cxx" || ext == ".C";
}

inline ParsedArgs parse_driver_args(int argc, char** argv) {
  ParsedArgs parsed;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.rfind("--vmp-collect=", 0) == 0) {
      parsed.collect_path = arg.substr(std::string("--vmp-collect=").size());
      continue;
    }
    parsed.compiler_args.push_back(arg);
    if (!arg.empty() && arg[0] != '-' && looks_like_source_file(arg)) {
      parsed.source_files.push_back(arg);
    }
  }
  return parsed;
}

inline int run_process(const std::vector<std::string>& args) {
  if (args.empty()) {
    throw std::runtime_error("empty process invocation");
  }
#if defined(_WIN32)
  std::vector<const char*> cargs;
  cargs.reserve(args.size() + 1);
  for (const auto& arg : args) {
    cargs.push_back(arg.c_str());
  }
  cargs.push_back(nullptr);
  return _spawnvp(_P_WAIT, cargs.front(), const_cast<char* const*>(cargs.data()));
#else
  std::vector<char*> cargs;
  cargs.reserve(args.size() + 1);
  for (const auto& arg : args) {
    cargs.push_back(const_cast<char*>(arg.c_str()));
  }
  cargs.push_back(nullptr);
  pid_t pid = fork();
  if (pid < 0) {
    throw std::runtime_error("fork failed");
  }
  if (pid == 0) {
    execvp(cargs[0], cargs.data());
    _exit(127);
  }
  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    throw std::runtime_error("waitpid failed");
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status);
  }
  return status;
#endif
}

inline void append_sanitized_compile_args(std::vector<std::string>& out, const std::vector<std::string>& compiler_args) {
  for (std::size_t i = 0; i < compiler_args.size(); ++i) {
    const std::string& arg = compiler_args[i];
    if (arg == "-o" || arg == "-MF" || arg == "-MT" || arg == "-MQ" || arg == "-MJ" ||
        arg == "-serialize-diagnostics" || arg == "-Xlinker") {
      ++i;
      continue;
    }
    if (arg == "-c" || arg == "-S" || arg == "-E") {
      continue;
    }
    if (arg.rfind("-Wl,", 0) == 0 || arg.rfind("-l", 0) == 0 || arg.rfind("-L", 0) == 0) {
      continue;
    }
    if (!arg.empty() && arg[0] != '-' && looks_like_source_file(arg)) {
      continue;
    }
    out.push_back(arg);
  }
}

inline std::vector<std::string> build_plugin_command(const DriverConfig& config,
                                                     const ParsedArgs& parsed,
                                                     const std::string& plugin_path) {
  std::vector<std::string> args;
  args.push_back(config.host_compiler);
  append_sanitized_compile_args(args, parsed.compiler_args);
  args.push_back("-fsyntax-only");
  for (const auto& source : parsed.source_files) {
    args.push_back(source);
  }
  args.push_back("-Xclang");
  args.push_back("-load");
  args.push_back("-Xclang");
  args.push_back(plugin_path);
  args.push_back("-Xclang");
  args.push_back("-plugin");
  args.push_back("-Xclang");
  args.push_back("vmp-collect");
  args.push_back("-Xclang");
  args.push_back("-plugin-arg-vmp-collect");
  args.push_back("-Xclang");
  args.push_back("-policy-out=" + parsed.collect_path);
  return args;
}

inline std::vector<std::string> build_fallback_command(const DriverConfig& config, const ParsedArgs& parsed) {
  std::vector<std::string> args;
  args.push_back(config.fallback_tool_path);
  args.push_back("--policy-out=" + parsed.collect_path);
  for (const auto& source : parsed.source_files) {
    args.push_back(source);
  }
  return args;
}

inline std::vector<std::string> build_collector_tool_command(const DriverConfig& config, const ParsedArgs& parsed) {
  std::vector<std::string> args;
  args.push_back(config.collector_tool_path);
  args.push_back("--policy-out=" + parsed.collect_path);
  for (const auto& source : parsed.source_files) {
    args.push_back(source);
  }
  args.push_back("--");
  append_sanitized_compile_args(args, parsed.compiler_args);
  return args;
}

inline int run_vmp_driver(const DriverConfig& config, int argc, char** argv) {
  const ParsedArgs parsed = parse_driver_args(argc, argv);
  std::vector<std::string> compiler_invocation;
  compiler_invocation.push_back(config.host_compiler);
  compiler_invocation.insert(compiler_invocation.end(), parsed.compiler_args.begin(), parsed.compiler_args.end());
  const int compiler_status = run_process(compiler_invocation);
  if (compiler_status != 0 || parsed.collect_path.empty()) {
    return compiler_status;
  }
  if (parsed.source_files.empty()) {
    throw std::runtime_error("--vmp-collect requires at least one source file argument");
  }

  const char* disable_plugin = std::getenv("VMP_DISABLE_CLANG_PLUGIN");
  const bool plugin_allowed = !(disable_plugin && std::string(disable_plugin) == "1");

  std::string plugin_path = config.plugin_path;
  if (const char* plugin_dir = std::getenv("VMP_PLUGIN_DIR"); plugin_dir != nullptr && !config.plugin_path.empty()) {
    plugin_path = (std::filesystem::path(plugin_dir) / std::filesystem::path(config.plugin_path).filename()).string();
  }

  if (plugin_allowed && !config.collector_tool_path.empty() && std::filesystem::exists(config.collector_tool_path)) {
    const int collector_status = run_process(build_collector_tool_command(config, parsed));
    if (collector_status == 0 && std::filesystem::exists(parsed.collect_path)) {
      return 0;
    }
  }

  if (plugin_allowed && !plugin_path.empty() && std::filesystem::exists(plugin_path)) {
    const int plugin_status = run_process(build_plugin_command(config, parsed, plugin_path));
    if (plugin_status == 0 && std::filesystem::exists(parsed.collect_path)) {
      return 0;
    }
  }

  return run_process(build_fallback_command(config, parsed));
}

}  // namespace vmp::tools
