# runtime/vm2

- 对应 plan：§6.2 / §6.3 / §6.4 / §9 / §16
- 当前状态：VM2 独立 ISA、解释器、跨域桥接、字符串句柄、CLI 工具已接线

## ISA 摘要
- 容器头：`VMP2` magic、version、flags、entry_pc、code_size、const_pool_size、`key_context_id[16]`
- 寄存器：
  - `r0..r31`：64-bit 整数寄存器
  - `q0..q15`：128-bit 向量寄存器（两条 `u64` lane）
  - `d0..d7`：double 寄存器
  - `p0..p7`：谓词寄存器
  - `pc/sp/lr`
- 栈：默认 128 KiB、16-byte 对齐、向下增长
- 调用：`blnk` 写入 `lr`，`bret` 通过 `lr` 返回；溢出整数参数落栈

## 指令族
- 整数：`iadd/isub/imul/idiv/imod/iand/ior/ixor/ishl/ishr/isar/ineg/inot`
- 向量：`vadd128/vsub128/vmul128/vxor128`
- 访存：`ildimm/vldimm/imov/imemld8/16/32/64/imemst8/16/32/64/vmemld128/vmemst128`
- 控制：`jmp/jp/jnp/blnk/bret/pcall/pret`
- 系统：`nop/brk/ftrap`
- 跨域：`xcall/xret`
- 字符串：`tsload/tsrelease`

## 跨域 ABI
复用 `runtime/vm1/include/vmp/runtime/bridge/bridge.h`：
- `native ↔ vm2`
- `vm1 ↔ vm2`
- `vm2 ↔ vm2`
- 默认 `max_depth = 64`

## 字符串句柄
- `tsload` 通过 `StringPool` 解密瞬时字符串并返回 VM2 handle
- `tsrelease` 显式释放 handle
- `bret/xret/异常 unwind` 自动清理未释放 handle
- 若模块 `key_context_id` 非零，`Vm2Context` 当前 key id 不匹配会触发 `string_pool_error`

## DSL / 工具
- 汇编：`runtime/vm2/asm/`
- 组装：`build/tools/vmp-vm2-asm input.vm2s output.vm2`
- 运行：`build/tools/vmp-vm2-run [--audit-path path] [--string-pool ... --string-idx ... --key-env ENV] module.vm2 [args...]`
