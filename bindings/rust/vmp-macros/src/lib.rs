use proc_macro::TokenStream;
use proc_macro2::Span;
use quote::ToTokens;
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, Once};
use syn::{ImplItemMethod, Item, LitStr, TraitItemMethod};

#[derive(Debug, Clone, PartialEq, Eq)]
struct AnnotationRecord {
    crate_name: String,
    kind: String,
    path: String,
    span_file: String,
    span_line: u32,
    extra_tags: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct ResolvedItem {
    kind: String,
    path: String,
    span_file: String,
    span_line: u32,
}

#[proc_macro_attribute]
pub fn vm_func(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let item_clone = item.clone();
    if let Err(err) = record_attribute(&item, "vm_func") {
        eprintln!("vmp-macros vm_func warning: {err}");
    }
    item_clone
}

#[proc_macro_attribute]
pub fn vm_string(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let item_clone = item.clone();
    if let Err(err) = record_attribute(&item, "vm_string") {
        eprintln!("vmp-macros vm_string warning: {err}");
    }
    item_clone
}

#[proc_macro]
pub fn vm_string_literal(input: TokenStream) -> TokenStream {
    let lit = syn::parse::<LitStr>(input.clone()).expect("vm_string_literal! expects a string literal");
    if let Err(err) = record_literal(&lit) {
        eprintln!("vmp-macros vm_string_literal warning: {err}");
    }
    lit.to_token_stream().into()
}

fn record_attribute(item: &TokenStream, attr_name: &str) -> Result<(), String> {
    let crate_name = std::env::var("CARGO_PKG_NAME").map_err(|e| format!("CARGO_PKG_NAME: {e}"))?;
    let manifest_dir = PathBuf::from(
        std::env::var("CARGO_MANIFEST_DIR").map_err(|e| format!("CARGO_MANIFEST_DIR: {e}"))?,
    );
    let resolved = resolve_tokens(item, &crate_name, &manifest_dir)?;
    let mut tags = vec![attr_name.to_string()];
    if attr_name == "vm_string" && matches!(resolved.kind.as_str(), "fn" | "method" | "trait_method" | "async_fn") {
        tags.push("vm_string_func_scope".to_string());
    }
    let record = AnnotationRecord {
        crate_name,
        kind: resolved.kind,
        path: resolved.path,
        span_file: resolved.span_file,
        span_line: resolved.span_line,
        extra_tags: tags,
    };
    append_record(&manifest_dir, &record)
}

fn record_literal(lit: &LitStr) -> Result<(), String> {
    let crate_name = std::env::var("CARGO_PKG_NAME").map_err(|e| format!("CARGO_PKG_NAME: {e}"))?;
    let manifest_dir = PathBuf::from(
        std::env::var("CARGO_MANIFEST_DIR").map_err(|e| format!("CARGO_MANIFEST_DIR: {e}"))?,
    );
    let (span_file, span_line) = discover_literal_location(&manifest_dir, &lit.value())
        .unwrap_or_else(|| (manifest_dir.join("src").join("lib.rs").to_string_lossy().into_owned(), 0));
    let path = format!(
        "literal::{}:{}|{}",
        span_file,
        span_line,
        quote_literal(&lit.value())
    );
    let record = AnnotationRecord {
        crate_name,
        kind: "literal".to_string(),
        path,
        span_file,
        span_line,
        extra_tags: vec!["vm_string".to_string()],
    };
    append_record(&manifest_dir, &record)
}

fn resolve_tokens(item: &TokenStream, crate_name: &str, manifest_dir: &Path) -> Result<ResolvedItem, String> {
    if let Ok(parsed) = syn::parse::<Item>(item.clone()) {
        return resolve_item(&parsed, crate_name, manifest_dir);
    }
    if let Ok(method) = syn::parse::<ImplItemMethod>(item.clone()) {
        return resolve_method(&method.sig.ident.to_string(), span_line(method.sig.ident.span()), "method", crate_name, manifest_dir);
    }
    if let Ok(method) = syn::parse::<TraitItemMethod>(item.clone()) {
        return resolve_method(&method.sig.ident.to_string(), span_line(method.sig.ident.span()), "trait_method", crate_name, manifest_dir);
    }
    let text = item.to_string();
    if let Some(name) = extract_fn_name_from_tokens(&text) {
        let kind = if text.trim_end().ends_with(';') { "trait_method" } else { "method" };
        return resolve_method(&name, 0, kind, crate_name, manifest_dir);
    }
    Err("vm_func/vm_string only support fn/impl fn/trait fn/const/static items".to_string())
}

fn extract_fn_name_from_tokens(text: &str) -> Option<String> {
    let idx = text.find("fn ")? + 3;
    let rest = &text[idx..];
    let mut name = String::new();
    for ch in rest.chars() {
        if ch.is_alphanumeric() || ch == '_' {
            name.push(ch);
        } else {
            break;
        }
    }
    if name.is_empty() { None } else { Some(name) }
}

fn resolve_method(name: &str, line: u32, kind: &str, crate_name: &str, manifest_dir: &Path) -> Result<ResolvedItem, String> {
    if let Some(found) = search_project_for_item(manifest_dir, crate_name, name, kind, line) {
        return Ok(found);
    }
    let fallback_file = default_source_file(manifest_dir);
    if let Ok(source) = fs::read_to_string(&fallback_file) {
        if let Ok(parsed) = syn::parse_file(&source) {
            for candidate in collect_candidates(crate_name, &fallback_file, &source, &parsed) {
                if candidate.path.rsplit("::").next() == Some(name)
                    && (candidate.kind == "method" || candidate.kind == "trait_method")
                {
                    return Ok(candidate);
                }
            }
        }
    }
    Ok(ResolvedItem {
        kind: kind.to_string(),
        path: format!("{crate_name}::{name}"),
        span_file: fallback_file.to_string_lossy().into_owned(),
        span_line: line,
    })
}

fn resolve_item(item: &Item, crate_name: &str, manifest_dir: &Path) -> Result<ResolvedItem, String> {
    let (hint_name, hint_line, direct_kind, direct_path) = match item {
        Item::Fn(item_fn) => {
            let span_line = span_line(item_fn.sig.ident.span());
            let kind = if item_fn.sig.asyncness.is_some() { "async_fn" } else { "fn" };
            (
                item_fn.sig.ident.to_string(),
                span_line,
                kind.to_string(),
                format!("{crate_name}::{}", item_fn.sig.ident),
            )
        }
        Item::Const(item_const) => {
            let span_line = span_line(item_const.ident.span());
            (
                item_const.ident.to_string(),
                span_line,
                "const".to_string(),
                format!("{crate_name}::{}", item_const.ident),
            )
        }
        Item::Static(item_static) => {
            let span_line = span_line(item_static.ident.span());
            (
                item_static.ident.to_string(),
                span_line,
                "static".to_string(),
                format!("{crate_name}::{}", item_static.ident),
            )
        }
        _ => return Err("vm_func/vm_string only support fn/const/static items in proc-macro frontend".to_string()),
    };

    if let Some(found) = search_project_for_item(manifest_dir, crate_name, &hint_name, &direct_kind, hint_line) {
        return Ok(found);
    }

    let fallback_file = default_source_file(manifest_dir);
    Ok(ResolvedItem {
        kind: direct_kind,
        path: direct_path,
        span_file: fallback_file.to_string_lossy().into_owned(),
        span_line: hint_line,
    })
}

fn quote_literal(value: &str) -> String {
    let mut out = String::with_capacity(value.len() + 2);
    out.push('"');
    for ch in value.chars() {
        match ch {
            '\\' => out.push_str("\\\\"),
            '"' => out.push_str("\\\""),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            other => out.push(other),
        }
    }
    out.push('"');
    out
}

fn record_path(manifest_dir: &Path, crate_name: &str) -> PathBuf {
    if let Ok(out_dir) = std::env::var("OUT_DIR") {
        return PathBuf::from(out_dir)
            .join("vmp_annotations")
            .join(format!("{crate_name}.tsv"));
    }
    manifest_dir
        .join("target")
        .join("vmp-annotations")
        .join(format!("{crate_name}.tsv"))
}


fn global_lock() -> &'static Mutex<()> {
    static INIT: Once = Once::new();
    static mut LOCK_PTR: *const Mutex<()> = std::ptr::null();
    INIT.call_once(|| {
        let lock = Box::new(Mutex::new(()));
        unsafe {
            LOCK_PTR = Box::into_raw(lock);
        }
    });
    unsafe { &*LOCK_PTR }
}

fn append_record(manifest_dir: &Path, record: &AnnotationRecord) -> Result<(), String> {
    let _guard = global_lock().lock().map_err(|_| "mutex poisoned".to_string())?;
    let path = record_path(manifest_dir, &record.crate_name);
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(|e| format!("create_dir_all {}: {e}", parent.display()))?;
    }
    let mut file = OpenOptions::new()
        .create(true)
        .append(true)
        .open(&path)
        .map_err(|e| format!("open {}: {e}", path.display()))?;
    let line = format!(
        "{}\t{}\t{}\t{}\t{}\t{}\n",
        escape_field(&record.crate_name),
        escape_field(&record.kind),
        escape_field(&record.path),
        escape_field(&record.span_file),
        record.span_line,
        escape_field(&record.extra_tags.join(","))
    );
    file.write_all(line.as_bytes())
        .map_err(|e| format!("write {}: {e}", path.display()))
}

fn escape_field(value: &str) -> String {
    let mut out = String::with_capacity(value.len());
    for ch in value.chars() {
        match ch {
            '\\' => out.push_str("\\\\"),
            '\t' => out.push_str("\\t"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            other => out.push(other),
        }
    }
    out
}

fn span_line(span: Span) -> u32 {
    span.start().line as u32
}

fn search_project_for_item(
    manifest_dir: &Path,
    crate_name: &str,
    target_name: &str,
    expected_kind: &str,
    target_line: u32,
) -> Option<ResolvedItem> {
    let mut best: Option<(i32, ResolvedItem)> = None;
    for file_path in candidate_rust_files(manifest_dir) {
        let source = match fs::read_to_string(&file_path) {
            Ok(v) => v,
            Err(_) => continue,
        };
        let parsed = match syn::parse_file(&source) {
            Ok(v) => v,
            Err(_) => continue,
        };
        for candidate in collect_candidates(crate_name, &file_path, &source, &parsed) {
            if candidate.path.rsplit("::").next() != Some(target_name) {
                continue;
            }
            if target_line != 0 && candidate.span_line != target_line {
                continue;
            }
            let score = match (expected_kind, candidate.kind.as_str(), candidate.path.contains(" for ")) {
                (ek, ck, _) if ek == ck => 40,
                ("fn", "trait_method", true) => 35,
                ("fn", "method", _) => 30,
                ("fn", "trait_method", false) => 25,
                ("method", "trait_method", true) => 35,
                ("method", "trait_method", false) => 25,
                _ => -1,
            };
            if score < 0 {
                continue;
            }
            let replace = match &best {
                Some((best_score, _)) => score > *best_score,
                None => true,
            };
            if replace {
                best = Some((score, candidate));
            }
        }
    }
    best.map(|(_, item)| item)
}

fn discover_literal_location(manifest_dir: &Path, literal_value: &str) -> Option<(String, u32)> {
    let needle = quote_literal(literal_value);
    for file_path in candidate_rust_files(manifest_dir) {
        let source = match fs::read_to_string(&file_path) {
            Ok(v) => v,
            Err(_) => continue,
        };
        for (idx, line) in source.lines().enumerate() {
            if line.contains(&needle) {
                return Some((file_path.to_string_lossy().into_owned(), (idx + 1) as u32));
            }
        }
    }
    None
}

fn candidate_rust_files(root: &Path) -> Vec<PathBuf> {
    let mut out = Vec::new();
    collect_rust_files(root, &mut out);
    out.sort();
    out
}

fn collect_rust_files(dir: &Path, out: &mut Vec<PathBuf>) {
    let iter = match fs::read_dir(dir) {
        Ok(v) => v,
        Err(_) => return,
    };
    for entry in iter.flatten() {
        let path = entry.path();
        let name = path.file_name().and_then(|s| s.to_str()).unwrap_or("");
        if path.is_dir() {
            if name == "target" || name.starts_with('.') {
                continue;
            }
            collect_rust_files(&path, out);
        } else if path.extension().and_then(|s| s.to_str()) == Some("rs") {
            out.push(path);
        }
    }
}

fn default_source_file(manifest_dir: &Path) -> PathBuf {
    let lib = manifest_dir.join("src").join("lib.rs");
    if lib.exists() {
        return lib;
    }
    manifest_dir.join("src").join("main.rs")
}

#[derive(Default)]
struct OccurrenceCounter(std::collections::BTreeMap<String, usize>);

impl OccurrenceCounter {
    fn next(&mut self, key: String) -> usize {
        let entry = self.0.entry(key).or_insert(0);
        *entry += 1;
        *entry
    }
}

fn collect_candidates(crate_name: &str, file_path: &Path, source: &str, file: &syn::File) -> Vec<ResolvedItem> {
    let mut out = Vec::new();
    let mut counter = OccurrenceCounter::default();
    collect_items(crate_name, file_path, source, &mut Vec::new(), &file.items, &mut counter, &mut out);
    out
}

fn collect_items(
    crate_name: &str,
    file_path: &Path,
    source: &str,
    modules: &mut Vec<String>,
    items: &[Item],
    counter: &mut OccurrenceCounter,
    out: &mut Vec<ResolvedItem>,
) {
    for item in items {
        match item {
            Item::Mod(item_mod) => {
                if let Some((_, nested)) = &item_mod.content {
                    modules.push(item_mod.ident.to_string());
                    collect_items(crate_name, file_path, source, modules, nested, counter, out);
                    modules.pop();
                }
            }
            Item::Fn(item_fn) => {
                let kind = if item_fn.sig.asyncness.is_some() { "async_fn" } else { "fn" };
                let prefix = qualified_prefix(crate_name, modules);
                let ident = item_fn.sig.ident.to_string();
                let line = find_occurrence_line(source, &format!("fn {}", ident), counter.next(format!("fn:{ident}")));
                out.push(ResolvedItem {
                    kind: kind.to_string(),
                    path: format!("{prefix}::{}", ident),
                    span_file: file_path.to_string_lossy().into_owned(),
                    span_line: line,
                });
            }
            Item::Const(item_const) => {
                let prefix = qualified_prefix(crate_name, modules);
                let ident = item_const.ident.to_string();
                let line = find_occurrence_line(source, &format!("const {}", ident), counter.next(format!("const:{ident}")));
                out.push(ResolvedItem {
                    kind: "const".to_string(),
                    path: format!("{prefix}::{}", ident),
                    span_file: file_path.to_string_lossy().into_owned(),
                    span_line: line,
                });
            }
            Item::Static(item_static) => {
                let prefix = qualified_prefix(crate_name, modules);
                let ident = item_static.ident.to_string();
                let line = find_occurrence_line(source, &format!("static {}", ident), counter.next(format!("static:{ident}")));
                out.push(ResolvedItem {
                    kind: "static".to_string(),
                    path: format!("{prefix}::{}", ident),
                    span_file: file_path.to_string_lossy().into_owned(),
                    span_line: line,
                });
            }
            Item::Trait(item_trait) => {
                let prefix = qualified_prefix(crate_name, modules);
                for trait_item in &item_trait.items {
                    if let syn::TraitItem::Method(method) = trait_item {
                        let ident = method.sig.ident.to_string();
                        let line = find_occurrence_line(source, &format!("fn {}", ident), counter.next(format!("fn:{ident}")));
                        out.push(ResolvedItem {
                            kind: "trait_method".to_string(),
                            path: format!("{prefix}::{}::{}", item_trait.ident, ident),
                            span_file: file_path.to_string_lossy().into_owned(),
                            span_line: line,
                        });
                    }
                }
            }
            Item::Impl(item_impl) => {
                let prefix = qualified_prefix(crate_name, modules);
                let self_ty = type_to_string(&item_impl.self_ty);
                let trait_name = item_impl
                    .trait_
                    .as_ref()
                    .map(|(_, path, _)| path.to_token_stream().to_string().replace(' ', ""));
                for impl_item in &item_impl.items {
                    if let syn::ImplItem::Method(method) = impl_item {
                        let ident = method.sig.ident.to_string();
                        let line = find_occurrence_line(source, &format!("fn {}", ident), counter.next(format!("fn:{ident}")));
                        let kind = if trait_name.is_some() { "trait_method" } else { "method" };
                        let path = if let Some(trait_name) = &trait_name {
                            format!("{prefix}::{} for {}::{}", trait_name, self_ty, ident)
                        } else {
                            format!("{prefix}::{}::{}", self_ty, ident)
                        };
                        out.push(ResolvedItem {
                            kind: kind.to_string(),
                            path,
                            span_file: file_path.to_string_lossy().into_owned(),
                            span_line: line,
                        });
                    }
                }
            }
            _ => {}
        }
    }
}

fn qualified_prefix(crate_name: &str, modules: &[String]) -> String {
    if modules.is_empty() {
        crate_name.to_string()
    } else {
        format!("{}::{}", crate_name, modules.join("::"))
    }
}

fn find_occurrence_line(source: &str, needle: &str, occurrence: usize) -> u32 {
    let mut seen = 0usize;
    for (idx, line) in source.lines().enumerate() {
        if line.contains(needle) {
            seen += 1;
            if seen == occurrence {
                return (idx + 1) as u32;
            }
        }
    }
    0
}

fn type_to_string(ty: &syn::Type) -> String {
    ty.to_token_stream().to_string().replace(' ', "")
}

#[cfg(test)]
mod tests {
    use super::*;
    use quote::quote;

    #[test]
    fn passthrough_tokens_match_for_fn() {
        let item = quote!(fn add(a: i32, b: i32) -> i32 { a + b });
        let parsed: Item = syn::parse2(item.clone()).unwrap();
        let reserialized = parsed.into_token_stream();
        assert_eq!(item.to_string(), reserialized.to_string());
    }

    #[test]
    fn escape_round_trip_examples() {
        assert_eq!(escape_field("a\tb"), "a\\tb");
        assert_eq!(quote_literal("secret key"), "\"secret key\"");
    }

    #[test]
    fn resolves_trait_impl_context() {
        let source = r#"
            pub trait Trait { fn m(&self); }
            pub struct Type;
            impl Trait for Type {
                fn m(&self) {}
            }
            "#;
        let file = syn::parse_file(source).unwrap();
        let entries = collect_candidates("demo", Path::new("src/lib.rs"), source, &file);
        assert!(entries.iter().any(|e| e.kind == "trait_method" && e.path.contains("Trait::m")));
        assert!(entries.iter().any(|e| e.kind == "trait_method" && e.path.contains("Trait for Type::m")));
    }
}
