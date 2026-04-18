#include <vmp/policy/policy_ir.h>

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Mangle.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>

#include <fstream>
#include <sstream>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using vmp::policy::AnnotationOrigin;
using vmp::policy::LanguageOrigin;
using vmp::policy::PolicyEntry;
using vmp::policy::PolicyIR;
using vmp::policy::SourceLocation;

struct CollectedDecl {
  std::string key;
  std::string display_name;
  std::string mangled_name;
  SourceLocation location;
  LanguageOrigin language_origin = LanguageOrigin::cpp;
  bool vm_func = false;
  bool vm_string = false;
};

std::string quote_display(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('"');
  for (char ch : text) {
    if (ch == '\\' || ch == '"') {
      out.push_back('\\');
    }
    out.push_back(ch);
  }
  out.push_back('"');
  return out;
}

bool has_annotation(const clang::Decl& decl, llvm::StringRef value) {
  for (const auto* attr : decl.attrs()) {
    if (const auto* annotate = llvm::dyn_cast<clang::AnnotateAttr>(attr)) {
      if (annotate->getAnnotation() == value) {
        return true;
      }
    }
  }
  return false;
}

std::string get_display_name(const clang::NamedDecl& decl) {
  std::string out;
  llvm::raw_string_ostream os(out);
  decl.printQualifiedName(os);
  return os.str();
}

std::string get_mangled_name(clang::ASTContext& context, const clang::NamedDecl& decl) {
  std::unique_ptr<clang::MangleContext> mangle_context(context.createMangleContext());
  if (!mangle_context->shouldMangleDeclName(&decl)) {
    return "";
  }

  clang::GlobalDecl global_decl(&decl);
  if (const auto* cxx_ctor = llvm::dyn_cast<clang::CXXConstructorDecl>(&decl)) {
    global_decl = clang::GlobalDecl(cxx_ctor, clang::Ctor_Complete);
  } else if (const auto* cxx_dtor = llvm::dyn_cast<clang::CXXDestructorDecl>(&decl)) {
    global_decl = clang::GlobalDecl(cxx_dtor, clang::Dtor_Complete);
  }

  std::string out;
  llvm::raw_string_ostream os(out);
  mangle_context->mangleName(global_decl, os);
  return os.str();
}

std::string to_symbol_or_region(const std::string& mangled_name, const std::string& display_name) {
  if (!mangled_name.empty() && mangled_name != display_name) {
    return mangled_name + "|" + display_name;
  }
  return display_name;
}


const clang::StringLiteral* find_string_literal(const clang::Stmt* stmt) {
  if (stmt == nullptr) {
    return nullptr;
  }
  if (const auto* literal = llvm::dyn_cast<clang::StringLiteral>(stmt)) {
    return literal;
  }
  for (const auto* child : stmt->children()) {
    if (const auto* found = find_string_literal(child)) {
      return found;
    }
  }
  return nullptr;
}

SourceLocation to_source_location(const clang::SourceManager& source_manager, clang::SourceLocation loc) {
  const auto spelling = source_manager.getSpellingLoc(loc);
  clang::PresumedLoc presumed = source_manager.getPresumedLoc(spelling);
  if (presumed.isInvalid()) {
    return {};
  }
  return SourceLocation{std::string(presumed.getFilename()), presumed.getLine(), presumed.getColumn()};
}

class CollectorVisitor : public clang::RecursiveASTVisitor<CollectorVisitor> {
 public:
  CollectorVisitor(clang::ASTContext& context, std::map<std::string, CollectedDecl>& entries)
      : context_(context), source_manager_(context.getSourceManager()), entries_(entries) {}

  bool VisitFunctionDecl(clang::FunctionDecl* decl) {
    if (!decl || !decl->getIdentifier() || !is_user_decl(*decl)) {
      return true;
    }
    const bool vm_func = has_annotation(*decl, "vmp_vm_func");
    const bool vm_string = has_annotation(*decl, "vmp_vm_string");
    if (!vm_func && !vm_string) {
      return true;
    }

    const std::string display_name = get_display_name(*decl);
    auto& out = entries_["func:" + display_name];
    out.key = "func:" + display_name;
    out.display_name = display_name;
    const std::string mangled = get_mangled_name(context_, *decl);
    if (!mangled.empty()) {
      out.mangled_name = mangled;
    }
    update_common(*decl, out, vm_func, vm_string);
    return true;
  }

  bool VisitVarDecl(clang::VarDecl* decl) {
    if (!decl || !decl->getIdentifier() || !is_user_decl(*decl)) {
      return true;
    }
    const bool vm_func = has_annotation(*decl, "vmp_vm_func");
    const bool vm_string = has_annotation(*decl, "vmp_vm_string");
    if (!vm_func && !vm_string) {
      return true;
    }

    const std::string display_name = get_display_name(*decl);
    auto& out = entries_["var:" + display_name];
    out.key = "var:" + display_name;
    out.display_name = display_name;
    const std::string mangled = get_mangled_name(context_, *decl);
    if (!mangled.empty()) {
      out.mangled_name = mangled;
    }
    update_common(*decl, out, vm_func, vm_string);

    if (vm_string && decl->hasInit()) {
      if (const auto* literal = find_string_literal(decl->getInit())) {
        add_literal(*literal);
      }
    }
    return true;
  }

 private:
  bool is_user_decl(const clang::Decl& decl) const {
    return source_manager_.isWrittenInMainFile(source_manager_.getSpellingLoc(decl.getLocation()));
  }

  void update_common(const clang::NamedDecl& decl, CollectedDecl& out, bool vm_func, bool vm_string) {
    out.vm_func = out.vm_func || vm_func;
    out.vm_string = out.vm_string || vm_string;
    out.language_origin = context_.getLangOpts().CPlusPlus ? LanguageOrigin::cpp : LanguageOrigin::c;
    const auto loc = to_source_location(source_manager_, decl.getLocation());
    if (out.location.line == 0 || (loc.line != 0 && loc.line < out.location.line)) {
      out.location = loc;
    }
  }

  void add_literal(const clang::StringLiteral& literal) {
    const auto loc = to_source_location(source_manager_, literal.getBeginLoc());
    const std::string key = "literal:" + loc.file + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column);
    auto& out = entries_[key];
    out.key = key;
    out.display_name = "literal::" + loc.file + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column) +
                       "|" + quote_display(literal.getString().str());
    out.location = loc;
    out.language_origin = context_.getLangOpts().CPlusPlus ? LanguageOrigin::cpp : LanguageOrigin::c;
    out.vm_string = true;
  }

  clang::ASTContext& context_;
  clang::SourceManager& source_manager_;
  std::map<std::string, CollectedDecl>& entries_;
};

class CollectorConsumer : public clang::ASTConsumer {
 public:
  explicit CollectorConsumer(std::map<std::string, CollectedDecl>& entries) : entries_(entries) {}

  void HandleTranslationUnit(clang::ASTContext& context) override {
    CollectorVisitor visitor(context, entries_);
    visitor.TraverseDecl(context.getTranslationUnitDecl());
  }

 private:
  std::map<std::string, CollectedDecl>& entries_;
};

class CollectorFrontendAction : public clang::ASTFrontendAction {
 public:
  explicit CollectorFrontendAction(std::map<std::string, CollectedDecl>& entries) : entries_(entries) {}

  std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance&, llvm::StringRef) override {
    return std::make_unique<CollectorConsumer>(entries_);
  }

 private:
  std::map<std::string, CollectedDecl>& entries_;
};

int write_policy(const std::map<std::string, CollectedDecl>& entries, const std::string& policy_out) {
  PolicyIR policy_ir;
  auto defaults = vmp::policy::default_policy_defaults();
  defaults.annotation_origin = AnnotationOrigin::attribute;
  policy_ir.defaults = defaults;

  for (const auto& [_, decl] : entries) {
    PolicyEntry entry;
    entry.symbol_or_region = to_symbol_or_region(decl.mangled_name, decl.display_name);
    entry.language_origin = decl.language_origin;
    entry.annotation_origin = AnnotationOrigin::attribute;
    entry.source_location = decl.location;
    if (decl.vm_func) {
      vmp::policy::apply_vm_func_annotation(entry);
    }
    if (decl.vm_string) {
      vmp::policy::apply_vm_string_annotation(entry);
    }
    policy_ir.entries.push_back(std::move(entry));
  }
  std::sort(policy_ir.entries.begin(), policy_ir.entries.end(), [](const PolicyEntry& lhs, const PolicyEntry& rhs) {
    return lhs.symbol_or_region < rhs.symbol_or_region;
  });
  vmp::policy::save_to_file(policy_ir, policy_out);
  return 0;
}

}  // namespace

int main(int argc, const char** argv) {
  try {
    std::string policy_out;
    std::vector<std::string> sources;
    std::vector<std::string> compile_args;
    bool after_dash_dash = false;
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (!after_dash_dash && arg == "--") {
        after_dash_dash = true;
        continue;
      }
      if (!after_dash_dash && arg.rfind("--policy-out=", 0) == 0) {
        policy_out = arg.substr(std::string("--policy-out=").size());
        continue;
      }
      if (after_dash_dash) {
        compile_args.push_back(arg);
      } else {
        sources.push_back(arg);
      }
    }
    if (policy_out.empty()) {
      throw std::runtime_error("--policy-out=<path> is required");
    }
    if (sources.empty()) {
      throw std::runtime_error("at least one source path is required");
    }
    std::map<std::string, CollectedDecl> entries;
    for (const auto& source : sources) {
      std::ifstream input(source);
      if (!input) {
        throw std::runtime_error("failed to open source file: " + source);
      }
      std::ostringstream buffer;
      buffer << input.rdbuf();
      auto action = std::make_unique<CollectorFrontendAction>(entries);
      if (!clang::tooling::runToolOnCodeWithArgs(std::move(action), buffer.str(), compile_args, source)) {
        return 1;
      }
    }
    return write_policy(entries, policy_out);
  } catch (const std::exception& ex) {
    llvm::errs() << "vmp-cpp-clang-collect: " << ex.what() << "\n";
    return 1;
  }
}
