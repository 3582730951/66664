use std::path::{Path, PathBuf};
use vmp_policy::PolicyIR;

fn repo_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../..")
        .canonicalize()
        .unwrap()
}

fn policy_fixture(name: &str) -> PathBuf {
    repo_root().join("tests").join("policy").join("examples").join(name)
}

fn find_cpp_summary_bin() -> PathBuf {
    if let Ok(path) = std::env::var("VMP_POLICY_CPP_SUMMARY_BIN") {
        let candidate = PathBuf::from(path);
        if candidate.is_file() {
            return candidate;
        }
    }

    let exe_name = format!("policy_cpp_summary{}", std::env::consts::EXE_SUFFIX);
    let root = repo_root();
    let mut candidates = Vec::new();
    for entry in std::fs::read_dir(&root).unwrap() {
        let entry = entry.unwrap();
        let path = entry.path();
        if path.is_dir() {
            let file_name = match path.file_name().and_then(|s| s.to_str()) {
                Some(file_name) => file_name,
                None => continue,
            };
            if file_name.starts_with("build") {
                candidates.push(path.join("tests").join(&exe_name));
            }
        }
    }
    candidates.sort();
    candidates
        .into_iter()
        .find(|candidate| candidate.is_file())
        .unwrap_or_else(|| panic!("failed to locate {} in repo build directories", exe_name))
}

#[test]
fn rust_reads_shared_fixture() {
    let fixture = policy_fixture("good.json");
    let policy = PolicyIR::load_from_file(&fixture).unwrap();
    let first = &policy.entries[0];
    assert_eq!(first.symbol_or_region, "secure::verify_score");
    assert_eq!(format!("{:?}", first.language_origin), "Cpp");
    assert_eq!(format!("{:?}", first.protection_domain), "Vm1");
    assert_eq!(format!("{:?}", first.sensitivity_level), "HighlySensitive");
    assert!(first.annotation_tags.iter().any(|tag| tag == "vm_func"));
    assert!(first.annotation_tags.iter().any(|tag| tag == "vm_string"));
}

#[test]
fn cpp_and_rust_summaries_match() {
    let fixture = policy_fixture("good.json");
    let rust_policy = PolicyIR::load_from_file(&fixture).unwrap();
    let first = &rust_policy.entries[0];
    let rust_summary = serde_json::json!({
        "schema_version": rust_policy.schema_version,
        "entry_count": rust_policy.entries.len(),
        "first": {
            "symbol_or_region": first.symbol_or_region,
            "language_origin": format!("{:?}", first.language_origin).to_lowercase(),
            "protection_domain": format!("{:?}", first.protection_domain).to_lowercase(),
            "sensitivity_level": "highly_sensitive",
            "annotation_tags": first.annotation_tags,
        }
    });
    let cpp_output = std::process::Command::new(find_cpp_summary_bin())
        .arg(&fixture)
        .output()
        .expect("run c++ summary helper");
    assert!(cpp_output.status.success(), "{}", String::from_utf8_lossy(&cpp_output.stderr));
    let cpp_summary: serde_json::Value = serde_json::from_slice(&cpp_output.stdout).unwrap();
    assert_eq!(cpp_summary, rust_summary);
}
