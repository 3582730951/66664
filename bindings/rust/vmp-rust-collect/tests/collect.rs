#[test]
fn parses_and_dedups() {
    let out = vmp_rust_collect::parse_tsv_line("demo\tfn\tdemo::f\tsrc/lib.rs\t3\tvm_func");
    assert!(out.is_ok());
}

#[test]
fn rejects_bad_format() {
    let err = vmp_rust_collect::parse_tsv_line("bad").unwrap_err();
    assert!(err.contains("malformed"));
}
