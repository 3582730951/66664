# runtime/cryptor

`runtime/cryptor/` 实现 subtask 27 的 **Rolling Opcode Map** 运行时层。

## 目标

在**不修改 subtask 23/24/25/26 持久化格式**的前提下，为 VM1 / VM2 的 opcode-encrypted 模块增加运行时 epoch 轮转能力：

- 维护 `current + previous` 两个 opcode-map epoch；
- 在以下事件发生时触发轮转：
  - key rotation
  - integrity event
  - VM1 ↔ VM2 domain switch
  - dispatch 计数兜底（默认 `2^18` 次 dispatch）
- 为 in-flight dispatch 保留上一代 map，避免轮转瞬间把仍在执行的旧块打断；
- 把 epoch 信息传递给 JIT cache key，保证旧 epoch 代码不能被新 epoch 复用。

## 目录

- `include/vmp/runtime/cryptor/rolling_policy.h`
  - 轮转域、原因、默认策略；
- `include/vmp/runtime/cryptor/rolling_opcode.h`
  - `OpcodeMap` / `OpcodeEpoch` / `OpcodeMapStore` / `RollingOpcodeRegistry`；
- `include/vmp/runtime/cryptor/rolling_opcode_vm1.h`
  - VM1 适配层：descriptor 构建、fetch、事件通知、epoch 查询；
- `include/vmp/runtime/cryptor/rolling_opcode_vm2.h`
  - VM2 适配层；
- `include/vmp/runtime/cryptor/jit_epoch.h`
  - `JitCacheKey {{ module_id, entry_pc, epoch_id }}`；
- `src/rolling_opcode.cpp`
  - registry / epoch storage / TLS in-flight scope / audit 实现。

## 核心数据结构

### `OpcodeMapStore`

每个模块仅保留两代：

- `current`
- `previous`

`previous` 只用于已经开始执行、但尚未退出的旧 epoch dispatch；新 fetch 默认读取 `current`。

### `DispatchEpochScope`

解释器在 dispatch 入口创建 scope，把 `(domain, module_id, epoch_id)` 压入 TLS 栈：

- 正常 fetch：读取当前 TLS epoch；
- 若轮转发生：后续新 dispatch 进入新 epoch；
- 旧 dispatch 在 scope 生命周期内仍用旧 epoch 解码。

## 轮转行为

### 初始 epoch

- epoch 0 基于模块当前 seed / key material 建立 opcode permutation；
- forward storage 使用 canonical forward code 重新编码；
- 若模块是 reverse-layout，则同步重建 reverse storage。

### bump 到下一代

下一代 epoch 会：

1. 从当前 epoch storage 解码出 canonical opcode；
2. 基于新 seed 生成新 permutation；
3. 把 canonical opcode 重新编码写回新的 forward / reverse storage；
4. `previous <- current`，`current <- next`。

## 触发点

### VM1

- `domain_call vm2, ...` 前：`notify_domain_switch()`
- key rotation / integrity event：通过 `notify_key_rotation()` / `notify_integrity_event()`
- basic-block dispatch 入口：`begin_dispatch()`

### VM2

- `xcall vm1, ...` 前：`notify_domain_switch()`
- bridge 调 VM2 前：`notify_domain_switch()`
- key rotation / integrity event：通过 `notify_key_rotation()` / `notify_integrity_event()`
- 每条 VM2 dispatch 入口：`begin_dispatch()`

### Dispatch budget fallback

默认策略：`dispatch_budget = 1 << 18`。

达到阈值时自动触发 `RotationReason::dispatch_budget`。

## JIT 集成

### VM1 block-JIT

- `JitCacheKey` 新增 `epoch_id`；
- epoch bump 时，VM1 采用**整模块全量 eviction**；
- 新 epoch 重新编译后会以新的 `(module_id, pc, epoch_id)` 进入 cache。

### VM2 function-JIT

- `JitCacheKey` 同样带 `epoch_id`；
- epoch bump 时，VM2 仅驱逐**旧 epoch tag** 的函数项；
- 对未启用 rolling epoch 的未加密模块，运行时事件仍保持原有 full invalidate 语义，避免回归旧测试。

## 审计

每次成功轮转都会写入：

- event type: `opcode_epoch_rotated`
- note fields:
  - `module_id=<id>`
  - `reason=<key_rotation|integrity_event|domain_switch|dispatch_budget>`
  - `new_epoch_id=<n>`

## 兼容性约束

- **不改** subtask 23/24/25/26 的 on-disk 格式；
- **不改** policy / planner / frontends / runtime state machine 接口语义；
- 对未加密模块，cryptor adapter 直接 short-circuit，避免影响既有 VM/JIT/异常路径。

## 测试

subtask 27 的新增测试集中在：

- `tests/runtime_rolling_opcode/`

覆盖内容包括：

- current + previous epoch store
- in-flight previous epoch fetch
- key / integrity / domain switch 触发
- `2^18` dispatch fallback
- VM1 全量 JIT eviction
- VM2 epoch-tag eviction
