#include <vmp/policy/policy_ir.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace vmp::bindings::cpp {
namespace {

using vmp::policy::AnnotationOrigin;
using vmp::policy::LanguageOrigin;
using vmp::policy::PolicyEntry;
using vmp::policy::PolicyIR;
using vmp::policy::SourceLocation;

struct PendingDecl {
  std::size_t start_line = 0;
  std::string text;
  bool saw_annotation = false;
};

struct PendingEntry {
  std::string key;
  std::string symbol_or_region;
  LanguageOrigin language_origin = LanguageOrigin::cpp;
  AnnotationOrigin annotation_origin = AnnotationOrigin::attribute;
  SourceLocation source_location;
  bool vm_func = false;
  bool vm_string = false;
};

bool has_vm_func_marker(const std::string& text) {
  return text.find("VMP_VM_FUNC") != std::string::npos || text.find("vmp_vm_func") != std::string::npos ||
         text.find("VM_func") != std::string::npos || text.find("[[vmp::vm_func]]") != std::string::npos;
}

bool has_vm_string_marker(const std::string& text) {
  return text.find("VMP_VM_STRING") != std::string::npos || text.find("vmp_vm_string") != std::string::npos ||
         text.find("VM_string") != std::string::npos || text.find("[[vmp::vm_string]]") != std::string::npos;
}

std::string trim(std::string value) {
  auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

bool looks_like_function_decl(const std::string& text) {
  if (text.find('(') == std::string::npos || text.find(')') == std::string::npos) {
    return false;
  }
  if (text.find("if(") != std::string::npos || text.find("for(") != std::string::npos ||
      text.find("while(") != std::string::npos || text.find("switch(") != std::string::npos) {
    return false;
  }
  return true;
}

std::optional<std::string> extract_function_name(const std::string& text) {
  static const std::regex kPattern(R"(([A-Za-z_~][A-Za-z0-9_:~]*)\s*\([^;{}]*\))");
  std::sregex_iterator it(text.begin(), text.end(), kPattern);
  std::sregex_iterator end;
  std::string best;
  for (; it != end; ++it) {
    const std::string token = (*it)[1].str();
    const auto open = token.find('(');
    const std::string name = trim(token.substr(0, open));
    if (name == "if" || name == "for" || name == "while" || name == "switch" || name == "return") {
      continue;
    }
    best = name;
  }
  if (best.empty()) {
    return std::nullopt;
  }
  return best;
}

std::optional<std::string> extract_variable_name(const std::string& text) {
  static const std::regex kPattern(R"(([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]*\])?\s*(?:=|;))");
  std::sregex_iterator it(text.begin(), text.end(), kPattern);
  std::sregex_iterator end;
  std::string best;
  for (; it != end; ++it) {
    best = (*it)[1].str();
  }
  if (best.empty()) {
    return std::nullopt;
  }
  return best;
}

std::optional<std::string> extract_string_literal(const std::string& text) {
  static const std::regex kPattern(R"("([^"\\]|\\.)*")");
  std::smatch match;
  if (std::regex_search(text, match, kPattern)) {
    return match[0].str();
  }
  return std::nullopt;
}

PolicyEntry materialize_entry(const PendingEntry& pending) {
  PolicyEntry entry;
  entry.symbol_or_region = pending.symbol_or_region;
  entry.language_origin = pending.language_origin;
  entry.annotation_origin = pending.annotation_origin;
  entry.source_location = pending.source_location;
  if (pending.vm_func) {
    vmp::policy::apply_vm_func_annotation(entry);
  }
  if (pending.vm_string) {
    vmp::policy::apply_vm_string_annotation(entry);
  }
  return entry;
}

void merge_entry(std::map<std::string, PendingEntry>& entries, const PendingEntry& candidate) {
  auto [it, inserted] = entries.emplace(candidate.key, candidate);
  if (!inserted) {
    it->second.vm_func = it->second.vm_func || candidate.vm_func;
    it->second.vm_string = it->second.vm_string || candidate.vm_string;
    if (it->second.source_location.line == 0 ||
        (candidate.source_location.line != 0 && candidate.source_location.line < it->second.source_location.line)) {
      it->second.source_location = candidate.source_location;
    }
  }
}

PendingEntry build_decl_entry(const std::filesystem::path& file_path,
                              std::size_t line_number,
                              const std::string& text,
                              LanguageOrigin language_origin) {
  PendingEntry entry;
  entry.language_origin = language_origin;
  entry.source_location = SourceLocation{file_path.string(), static_cast<std::uint32_t>(line_number), 1u};
  entry.vm_func = has_vm_func_marker(text);
  entry.vm_string = has_vm_string_marker(text);
  if (text.find("#pragma") != std::string::npos) {
    entry.annotation_origin = AnnotationOrigin::pragma;
  }

  if (looks_like_function_decl(text)) {
    const auto name = extract_function_name(text);
    if (!name) {
      throw std::runtime_error("failed to parse annotated function declaration in " + file_path.string());
    }
    entry.key = "func:" + *name;
    entry.symbol_or_region = *name;
    return entry;
  }

  const auto name = extract_variable_name(text);
  if (!name) {
    throw std::runtime_error("failed to parse annotated variable declaration in " + file_path.string());
  }
  entry.key = "var:" + *name;
  entry.symbol_or_region = *name;
  return entry;
}

std::optional<PendingEntry> maybe_build_literal_entry(const std::filesystem::path& file_path,
                                                      std::size_t line_number,
                                                      const std::string& text,
                                                      LanguageOrigin language_origin,
                                                      bool vm_string) {
  if (!vm_string) {
    return std::nullopt;
  }
  const auto literal = extract_string_literal(text);
  if (!literal) {
    return std::nullopt;
  }
  PendingEntry entry;
  entry.key = "literal:" + file_path.string() + ":" + std::to_string(line_number);
  entry.symbol_or_region = "literal::" + file_path.string() + ":" + std::to_string(line_number) + "|" + *literal;
  entry.language_origin = language_origin;
  entry.annotation_origin = AnnotationOrigin::attribute;
  entry.source_location = SourceLocation{file_path.string(), static_cast<std::uint32_t>(line_number), 1u};
  entry.vm_string = true;
  return entry;
}

}  // namespace

PolicyIR collect_policy_from_sources_with_fallback(const std::vector<std::string>& sources) {
  PolicyIR policy_ir;
  auto defaults = vmp::policy::default_policy_defaults();
  defaults.annotation_origin = AnnotationOrigin::attribute;
  defaults.plaintext_budget = vmp::policy::PlaintextBudget::transient_only;
  policy_ir.defaults = defaults;

  std::map<std::string, PendingEntry> collected;
  for (const auto& source : sources) {
    const std::filesystem::path path(source);
    std::ifstream input(path);
    if (!input) {
      throw std::runtime_error("failed to open source file: " + source);
    }
    const auto extension = path.extension().string();
    const bool is_c = extension == ".c";
    const LanguageOrigin language_origin = is_c ? LanguageOrigin::c : LanguageOrigin::cpp;

    std::string line;
    PendingDecl pending;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
      ++line_number;
      const bool line_has_annotation = has_vm_func_marker(line) || has_vm_string_marker(line);
      if (line_has_annotation && !pending.saw_annotation) {
        pending = PendingDecl{line_number, line, true};
      } else if (pending.saw_annotation) {
        pending.text.append("\n").append(line);
      }

      if (!pending.saw_annotation) {
        continue;
      }

      const std::string compact = trim(line);
      const bool terminates = compact.find(';') != std::string::npos || compact.find('{') != std::string::npos;
      if (!terminates) {
        continue;
      }

      PendingEntry decl = build_decl_entry(path, pending.start_line, pending.text, language_origin);
      merge_entry(collected, decl);
      if (auto literal = maybe_build_literal_entry(path, pending.start_line, pending.text, language_origin, decl.vm_string)) {
        merge_entry(collected, *literal);
      }
      pending = PendingDecl{};
    }
  }

  for (const auto& [_, pending] : collected) {
    policy_ir.entries.push_back(materialize_entry(pending));
  }
  std::sort(policy_ir.entries.begin(), policy_ir.entries.end(), [](const PolicyEntry& lhs, const PolicyEntry& rhs) {
    return lhs.symbol_or_region < rhs.symbol_or_region;
  });
  return policy_ir;
}

}  // namespace vmp::bindings::cpp
