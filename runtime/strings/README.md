# runtime/strings

- 对应 plan：§9 / §7.6-§7.7 / §14 / §16
- 目标：静态无明文、运行时仅瞬时明文、用后立刻擦除。

## 加密格式

每条字符串记录独立存入 `string_pool.bin`，索引保存在 `string_pool.idx.json`。

### EBNF

```ebnf
string_pool      = { record } ;
record           = encrypted_payload , auth_tag ;
encrypted_payload= chacha20( length_prefix , plaintext ) ;
length_prefix    = uint32_le ;
auth_tag         = hmac_sha256( nonce || encrypted_payload ) ;
nonce            = 12OCTET ;
```

- `key = HKDF-SHA256(master_key, salt, "string-pool")`
- `cipher = ChaCha20(key, nonce, counter=0)`
- `auth_tag = HMAC-SHA256(key, nonce || encrypted_payload)`

## Key 派生

1. `master_key` 不落盘，只通过 provider / 环境变量 / stdin 临时物化。
2. `KeyContext::derive_subkey(purpose_tag)`：
   - `PRK = HKDF-Extract(salt, master_key)`
   - `OKM = HKDF-Expand(PRK, purpose_tag, 32)`
3. `master_key`、`PRK`、`OKM` 在使用结束后立即零化。

## RAII 擦除

- `TransientView` 小于等于 4 KiB 时使用对象内联缓冲（通常位于调用栈）。
- 超过 4 KiB 时使用 `mmap + mlock` 临时页。
- 析构时执行 `secure_memzero()`，随后对大页执行 `munlock + munmap`。
- `secure_memzero()` 使用 `volatile` 写入 + compiler barrier，避免被优化掉。

## VM1 opcode

- `load_tstr vr0, &sid42`
  - 从关联 `StringPool` 解密 `string_id=42`
  - 返回一个 VM 侧 transient handle 到 `vr0`
- `release_tstr vr0`
  - 立即销毁 handle，触发擦除
- 若未显式释放，函数返回或异常退栈时 VM1 自动清理当前帧持有的 transient views

> JIT hook：后续 JIT 接入点必须保证 transient view 的明文结果不会被常量传播进缓存。本轮仅保留注释 hook，不实现真实 JIT 逻辑。

## Tools

### vmp-string-protect

```bash
VMP_STRING_MASTER_KEY=<64hex> build/tools/vmp-string-protect \
  --policy policy.json \
  --out-bin string_pool.bin \
  --out-idx string_pool.idx.json \
  --out-kdf key_derivation.json
```

- `policy.json` 中带 `annotation_tags: ["vm_string"]` 的条目可额外提供：
  - `string_id: <u32>`
  - `value: "plaintext"`

### vmp-protect --protect-strings

```bash
VMP_STRING_MASTER_KEY=<64hex> build/tools/vmp-protect \
  --policy policy.json \
  --emit-policy-json policy.out.json \
  --protect-strings \
  --string-bin string_pool.bin \
  --string-idx string_pool.idx.json \
  --string-kdf key_derivation.json
```

### vmp-vm1-run

```bash
VMP_STRING_MASTER_KEY=<64hex> build/tools/vmp-vm1-run \
  --string-pool string_pool.bin \
  --string-idx string_pool.idx.json \
  --key-env VMP_STRING_MASTER_KEY \
  module.vm1
```

## 限制

- 当前仅解释器路径接入；JIT 只保留约束注释。
- 索引 JSON 会携带 salt / KDF 元数据，便于 runtime loader 在不额外读取 metadata 文件的情况下恢复 `KeyContext`。
- `VMP_STRING_USE` 依赖 `ScopedCurrentPool` 绑定当前线程的 `StringPool`。
