# arch/common

- 统一 lifting / decoder 共享层。
- `pc_relative.h` 在 subtask 20 引入 `PcRelativeTarget` 与 `Label`：
  - `source_pc`：该 ISA 计算位移时使用的基准 PC。
    - x86/x64 branch/call/RIP-relative：下一条指令地址
    - ARM32 branch/literal：`PC+8`
    - Thumb branch/literal：`PC+4`（literal 再按 ISA 对齐）
    - ARM64 branch/ADR/ADRP/literal：当前指令地址
  - `displacement`：统一按字节保存；`ADRP` 的 page-relative 形式保存为 `4096` 的倍数。
  - `computed_absolute`：按 ISA 语义计算出的绝对目标。
  - `kind`：`branch / call / load / store / address_materialize / indirect_jump_via_table`
- `Label` 仅提供名称与可选 `resolved_vm_pc`，真正的跨 basic-block 解析在 subtask 21 落地。
- helper：
  - `encode_rel8/16/32(source_pc, target_pc)`：返回位移并做范围检查。
  - `encode_pc_page(source_pc, target_pc)`：按 4 KiB page 计算 byte displacement，并检查是否满足 ADRP 的 21-bit page-range。
