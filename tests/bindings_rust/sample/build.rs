fn main() {
    let out_dir = std::env::var("OUT_DIR").expect("OUT_DIR");
    println!("cargo:rustc-env=OUT_DIR={out_dir}");
    let crate_name = std::env::var("CARGO_PKG_NAME").expect("CARGO_PKG_NAME");
    let out_path = std::path::Path::new(&out_dir).join("vmp_annotations").join(format!("{crate_name}.tsv"));
    let _ = std::fs::remove_file(&out_path);
    let fallback = std::path::Path::new(&std::env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR"))
        .join("target")
        .join("vmp-annotations")
        .join(format!("{crate_name}.tsv"));
    let _ = std::fs::remove_file(fallback);
}
