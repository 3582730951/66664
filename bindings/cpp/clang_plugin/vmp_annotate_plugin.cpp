#include <vmp/policy/policy_ir.h>

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Mangle.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Lex/Preprocessor.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace vmp::bindings::cpp {
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

LanguageOrigin infer_language_from_file(const std::string& file) {
  const std::string extension = std::filesystem::path(file).extension().string();
  if (extension == ".c") {
    return LanguageOrigin::c;
  }
  return LanguageOrigin::cpp;
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
    const auto loc = to_source_location(source_manager_, decl.getLocation());
    out.language_origin = infer_language_from_file(loc.file);
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
    out.language_origin = infer_language_from_file(loc.file);
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

class CollectorAction : public clang::PluginASTAction {
 public:
  std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& compiler,
                                                        llvm::StringRef) override {
    compiler_ = &compiler;
    return std::make_unique<CollectorConsumer>(entries_);
  }

  bool ParseArgs(const clang::CompilerInstance&, const std::vector<std::string>& args) override {
    for (const auto& arg : args) {
      if (arg.rfind("-policy-out=", 0) == 0) {
        policy_out_ = arg.substr(std::string("-policy-out=").size());
      }
    }
    return !policy_out_.empty();
  }

  PluginASTAction::ActionType getActionType() override { return AddAfterMainAction; }

  void EndSourceFileAction() override {
    PolicyIR policy_ir;
    auto defaults = vmp::policy::default_policy_defaults();
    defaults.annotation_origin = AnnotationOrigin::attribute;
    policy_ir.defaults = defaults;

    for (const auto& [_, decl] : entries_) {
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
    vmp::policy::save_to_file(policy_ir, policy_out_);
  }

  static void PrintHelp(llvm::raw_ostream& ros) {
    ros << "vmp-collect plugin args: -policy-out=<path>\n";
  }

 private:
  clang::CompilerInstance* compiler_ = nullptr;
  std::string policy_out_;
  std::map<std::string, CollectedDecl> entries_;
};

}  // namespace
}  // namespace vmp::bindings::cpp

static clang::FrontendPluginRegistry::Add<vmp::bindings::cpp::CollectorAction> X("vmp-collect",
                                                                                  "collect VMP annotations");
