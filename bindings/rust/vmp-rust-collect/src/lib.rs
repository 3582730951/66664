use std::collections::BTreeSet;
use std::fs;
use std::path::{Path, PathBuf};
use vmp_policy::{
    AnnotationOrigin, PlaintextBudget, PolicyDefaults, PolicyEntry, PolicyIR, ProtectionDomain,
    SensitivityLevel, SourceLocation,
};

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub struct TsvRecord {
    pub crate_name: String,
    pub kind: String,
    pub path: String,
    pub span_file: String,
    pub span_line: u32,
    pub extra_tags: Vec<String>,
}

pub fn parse_tsv_line(line: &str) -> Result<TsvRecord, String> {
    let parts: Vec<_> = line.split('\t').collect();
    if parts.len() != 6 {
        return Err("malformed TSV line: expected 6 columns".to_string());
    }
    let crate_name = unescape(parts[0])?;
    let kind = unescape(parts[1])?;
    let path = unescape(parts[2])?;
    let span_file = unescape(parts[3])?;
    let span_line = parts[4]
        .parse::<u32>()
        .map_err(|_| "malformed TSV line: span_line must be u32".to_string())?;
    let extra_tags = unescape(parts[5])?
        .split(',')
        .filter(|s| !s.is_empty())
        .map(|s| s.to_string())
        .collect::<Vec<_>>();
    Ok(TsvRecord { crate_name, kind, path, span_file, span_line, extra_tags })
}

fn unescape(value: &str) -> Result<String, String> {
    let mut out = String::with_capacity(value.len());
    let mut chars = value.chars();
    while let Some(ch) = chars.next() {
        if ch != '\\' {
            out.push(ch);
            continue;
        }
        match chars.next() {
            Some('t') => out.push('\t'),
            Some('n') => out.push('\n'),
            Some('r') => out.push('\r'),
            Some('\\') => out.push('\\'),
            Some(other) => return Err(format!("unsupported escape sequence: \\{other}")),
            None => return Err("trailing backslash in TSV field".to_string()),
        }
    }
    Ok(out)
}

pub fn collect_tsv_files(target_dir: &Path) -> Result<Vec<PathBuf>, String> {
    let mut files = Vec::new();
    walk(target_dir, &mut files)?;
    files.sort();
    Ok(files)
}

fn walk(dir: &Path, files: &mut Vec<PathBuf>) -> Result<(), String> {
    for entry in fs::read_dir(dir).map_err(|e| format!("read_dir {}: {e}", dir.display()))? {
        let entry = entry.map_err(|e| format!("read_dir entry {}: {e}", dir.display()))?;
        let path = entry.path();
        if path.is_dir() {
            walk(&path, files)?;
        } else if path.extension().and_then(|s| s.to_str()) == Some("tsv")
            && path.components().any(|c| {
                let s = c.as_os_str().to_string_lossy();
                s == "vmp_annotations" || s == "vmp-annotations"
            })
        {
            files.push(path);
        }
    }
    Ok(())
}

pub fn read_records(target_dir: &Path) -> Result<Vec<TsvRecord>, String> {
    let mut dedup = BTreeSet::new();
    let mut out = Vec::new();
    for file in collect_tsv_files(target_dir)? {
        let text = fs::read_to_string(&file).map_err(|e| format!("read {}: {e}", file.display()))?;
        for (index, raw_line) in text.lines().enumerate() {
            if raw_line.trim().is_empty() {
                continue;
            }
            let rec = parse_tsv_line(raw_line)
                .map_err(|e| format!("{}:{}: {e}", file.display(), index + 1))?;
            let key = (rec.crate_name.clone(), rec.kind.clone(), rec.path.clone(), rec.span_line);
            if dedup.insert(key) {
                out.push(rec);
            }
        }
    }
    Ok(out)
}

pub fn records_to_policy(records: &[TsvRecord]) -> PolicyIR {
    let mut entries = Vec::new();
    for record in records {
        let mut entry = PolicyEntry {
            symbol_or_region: record.path.clone(),
            language_origin: vmp_policy::LanguageOrigin::Rust,
            annotation_origin: AnnotationOrigin::ProcMacro,
            protection_domain: ProtectionDomain::Native,
            jit_policy: vmp_policy::JitPolicy::Off,
            plaintext_budget: PlaintextBudget::TransientOnly,
            reaction_policy: vmp_policy::ReactionPolicy::Log,
            integrity_level: vmp_policy::IntegrityLevel::None,
            platform_caps: Vec::new(),
            sensitivity_level: SensitivityLevel::Normal,
            profile_seed: 0,
            mobile_bridge_mode: vmp_policy::MobileBridgeMode::Off,
            source_location: SourceLocation { file: record.span_file.clone(), line: record.span_line, column: 0 },
            annotation_tags: Vec::new(),
            event_types: Vec::new(),
        };
        entry.annotation_tags.push(format!("rust_kind:{}", record.kind));
        for tag in &record.extra_tags {
            if !entry.annotation_tags.contains(tag) {
                entry.annotation_tags.push(tag.clone());
            }
        }
        if record.extra_tags.iter().any(|tag| tag == "vm_func") {
            vmp_policy::apply_vm_func_annotation(&mut entry);
        }
        if record.extra_tags.iter().any(|tag| tag == "vm_string") {
            vmp_policy::apply_vm_string_annotation(&mut entry);
        }
        entries.push(entry);
    }
    entries.sort_by(|a, b| a.symbol_or_region.cmp(&b.symbol_or_region));
    PolicyIR {
        schema_version: vmp_policy::CURRENT_SCHEMA_VERSION,
        defaults: PolicyDefaults {
            language_origin: vmp_policy::LanguageOrigin::Rust,
            annotation_origin: AnnotationOrigin::ProcMacro,
            ..PolicyDefaults::default()
        },
        entries,
    }
}

pub fn collect_policy_from_target_dir(target_dir: &Path) -> Result<PolicyIR, String> {
    Ok(records_to_policy(&read_records(target_dir)?))
}

pub fn save_policy(path: &Path, policy: &PolicyIR) -> Result<(), String> {
    let json = serde_json::to_string_pretty(&serde_json::json!({
        "schema_version": policy.schema_version,
        "defaults": {
            "language_origin": "rust",
            "annotation_origin": "proc_macro",
            "protection_domain": "native",
            "jit_policy": "off",
            "plaintext_budget": "transient_only",
            "reaction_policy": "log",
            "integrity_level": "none",
            "platform_caps": [],
            "sensitivity_level": "normal",
            "profile_seed": 0,
            "mobile_bridge_mode": "off",
            "event_types": []
        },
        "entries": policy.entries.iter().map(|e| serde_json::json!({
            "symbol_or_region": e.symbol_or_region,
            "language_origin": "rust",
            "annotation_origin": "proc_macro",
            "protection_domain": to_domain(e.protection_domain),
            "plaintext_budget": to_budget(e.plaintext_budget),
            "sensitivity_level": to_sensitivity(e.sensitivity_level),
            "annotation_tags": e.annotation_tags,
            "source_location": {"file": e.source_location.file, "line": e.source_location.line, "column": 0}
        })).collect::<Vec<_>>()
    }))
    .map_err(|e| format!("serialize policy: {e}"))?;
    fs::write(path, json).map_err(|e| format!("write {}: {e}", path.display()))
}

fn to_domain(value: ProtectionDomain) -> &'static str {
    match value {
        ProtectionDomain::Native => "native",
        ProtectionDomain::Vm1 => "vm1",
        ProtectionDomain::Vm2 => "vm2",
    }
}

fn to_budget(value: PlaintextBudget) -> &'static str {
    match value {
        PlaintextBudget::None => "none",
        PlaintextBudget::TransientOnly => "transient_only",
    }
}

fn to_sensitivity(value: SensitivityLevel) -> &'static str {
    match value {
        SensitivityLevel::Normal => "normal",
        SensitivityLevel::Sensitive => "sensitive",
        SensitivityLevel::HighlySensitive => "highly_sensitive",
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::tempdir;

    #[test]
    fn parse_and_dedup_records() {
        let dir = tempdir().unwrap();
        let tsv_dir = dir.path().join("debug/build/x/out/vmp_annotations");
        fs::create_dir_all(&tsv_dir).unwrap();
        fs::write(
            tsv_dir.join("demo.tsv"),
            "demo\tfn\tdemo::f\tsrc/lib.rs\t3\tvm_func\n\ndemo\tfn\tdemo::f\tsrc/lib.rs\t3\tvm_func\n",
        )
        .unwrap();
        let records = read_records(dir.path()).unwrap();
        assert_eq!(records.len(), 1);
        let policy = records_to_policy(&records);
        assert_eq!(policy.entries.len(), 1);
        assert!(policy.entries[0].annotation_tags.iter().any(|t| t == "vm_func"));
    }

    #[test]
    fn rejects_bad_format_with_file_line() {
        let dir = tempdir().unwrap();
        let tsv_dir = dir.path().join("vmp-annotations");
        fs::create_dir_all(&tsv_dir).unwrap();
        let path = tsv_dir.join("broken.tsv");
        fs::write(&path, "bad\n").unwrap();
        let err = read_records(dir.path()).unwrap_err();
        assert!(err.contains(path.to_string_lossy().as_ref()));
        assert!(err.contains(":1:"));
    }
}
