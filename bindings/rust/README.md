# bindings/rust

对应 plan：§1、§2.1、§5.2、§5.3、§11。

## Crates

- `bindings/rust/vmp-macros`
  - `#[vm_func]`
  - `#[vm_string]`
  - `vm_string_literal!("...")` hidden helper for literal-site collection
- `bindings/rust/vmp-rust-collect`
  - 扫描 `target` 下的 Rust TSV side-channel 记录
  - 聚合为 schema v1 Policy IR JSON

## Build helper

Rust proc-macro 在展开时默认拿不到 consuming crate 的 `OUT_DIR`，因此需要在使用方 `build.rs` 里透传：

```rust
fn main() {
    let out_dir = std::env::var("OUT_DIR").expect("OUT_DIR");
    println!("cargo:rustc-env=OUT_DIR={out_dir}");
}
```

这样 `vmp-macros` 会写入：

- `${OUT_DIR}/vmp_annotations/<crate>.tsv`

若 `OUT_DIR` 不可见，则回退到：

- `${CARGO_MANIFEST_DIR}/target/vmp-annotations/<crate>.tsv`

## Usage

### Function / method annotation

```rust
use vmp_macros::vm_func;

#[vm_func]
fn add(a: i32, b: i32) -> i32 { a + b }
```

语义：

- `vm_func` → `protection_domain = vm1`
- `async fn` 会记录为 `kind = async_fn`
- trait impl method 会记录为 `kind = trait_method`，路径尽量嵌入 trait 名

### Const / static string annotation

```rust
#[vmp_macros::vm_string]
const GREETING: &str = "hello";
```

语义：

- `sensitivity_level = highly_sensitive`
- `plaintext_budget = transient_only`

### Literal-site annotation

Rust 的 attribute macro 与 bang macro 同名导出存在语言层命名限制，因此 literal site 通过一层本地 `macro_rules!` wrapper 调用隐藏 helper：

```rust
macro_rules! vm_string {
    ($text:literal) => {
        vmp_macros::vm_string_literal!($text)
    };
}

fn uses_literal() -> &'static str {
    vm_string!("secret key")
}
```

collector 会把该 literal 记录为 `kind = literal`。

## Collector CLI

```bash
cargo run -p vmp-rust-collect -- \
  --target-dir /path/to/target \
  --policy-out /tmp/rust-policy.json
```

输出 JSON 为 schema v1，可直接与 `vmp-protect --rust-target-dir ...` 合并。

## vmp-protect integration

```bash
build/tools/vmp-protect \
  --policy tests/bindings_rust/base_policy.json \
  --rust-target-dir target \
  --emit-policy-json /tmp/merged.json \
  --validate-only
```
