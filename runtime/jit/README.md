# runtime/jit

- 对应 plan：§7、§16
- 当前状态：VM1 block-level / trace-level JIT 与 VM2 function-level JIT 均已接入。

## 架构

`Vm1Jit` 位于 `runtime/jit/include/vmp/runtime/jit/vm1_jit.h`，负责：

- 每模块代码缓存与 `(module_id, block_start_pc) -> compiled entry` 映射
- 首次执行触发 block 编译，第二次命中走 JIT entry trampoline
- 热块（默认命中 64 次）记录稳定 trace；同一 trace 默认稳定 16 次后升级为 trace super-block
- 每模块默认 `8 MiB` 预算；超额时按 LRU 驱逐并记录审计事件
- `key_rotation` / `integrity_failure` / `policy_change` / `memory_pressure` / `domain_switch_request` / detector 事件失效接口

`Vm2Jit` 位于 `runtime/jit/include/vmp/runtime/jit/vm2_jit.h`，负责：

- 每模块函数级代码缓存与 `(module_id, entry_pc) -> compiled entry` 映射
- 仅以函数入口作为编译单元；函数内部 basic block 可被发现，但不会单独暴露 block JIT 入口
- 默认热度阈值 `32`，每模块默认预算 `4 MiB`
- 安装时基于 `KeyContext::derive_subkey("vm2_jit_integrity")` 生成每入口 `HMAC-SHA256(module_id || entry_pc || compiled_machine_code)` 完整性标签
- 每次从解释器切入已编译函数前都重新验证完整性；失败即自失效、记审计并回退解释执行
- `COMPILING / READY / INVALIDATED / EVICTED` 生命周期与 key-context 变更失效

## 后端

### 1. portable C backend (`VMP_JIT_BACKEND=c`)

- 生成临时 C 源文件到每进程缓存目录
- 调用 `cc -shared -O2 -fPIC` 产出 `.so`
- `dlopen + dlsym` 获取 `vmp_vm1_jit_entry`
- 语义层面仍严格通过 VM1 helper 执行，不降级到 native 入口

### 2. x86_64 emitter backend (`VMP_JIT_BACKEND=x64`)

- 仅在 Linux x86_64 主机默认启用
- 使用自有机器码 emitter 将 tiny stub 写入 `mmap(PROT_READ|PROT_WRITE)` 区域
- 完成写入后 `mprotect(PROT_READ|PROT_EXEC)`，遵守 W^X
- 当前覆盖：整数算术、load/store、call/ret、branch block 的 block/trace stub；不支持的 block 自动回退 C backend 并记录 `jit_fallback_backend`

VM2 也复用同一环境变量：

- `VMP_JIT_BACKEND=c`：为每个 VM2 函数入口生成单独 C translation unit，导出 `vmp_vm2_jit_entry`
- `VMP_JIT_BACKEND=x64`：在 Linux x86_64 上生成接收 `Vm2Context*` 的 tiny trampoline；仅接受 VM2 的整数/128-bit 向量子集，其他函数自动记录 `vm2_jit_fallback_backend` 并退回 C backend

## 代码缓存

- x64 backend：每 entry 单独 RW→RX `mmap` 页
- C backend：每 entry 对应临时 `.c/.so` 文件 + 已加载 handle
- 预算统计统一按 entry 估算 native code size 计入 per-module cache budget

## 失效 API

- `Vm1Jit::invalidate_all()`
- `Vm1Jit::invalidate_module(module_id)`
- `Vm1Jit::invalidate_entry(module_id, pc)`
- `Vm2Jit::invalidate_all()`
- `Vm2Jit::invalidate_module(module_id)`
- `Vm2Jit::invalidate_entry(module_id, pc)`
- `Vm2Jit::invalidate_on_event(key_rotated / integrity_failed / detection_event)`
- `RuntimeState::observe(key_rotated / integrity_failed)` 会调用 `invalidate_all()`
- `RuntimeState::detector_invalidate_module(module_id)` 用于 detector 定向失效

## 禁止优化（plan §7.7）

- 不将 `VM_func` 入口降级回 native
- `load_transient_string` / `release_transient_string` 保持为 JIT barrier；不缓存解密明文
- 不跨 `domain_call` / `domain_ret` 做常量传播或 inlining
- trace/block JIT 只做 VM1 内部执行路径优化，语义仍通过 VM1 上下文与 helper 保持一致
- VM2 JIT 不删除看似“死掉”的 predicate 更新；`xcall` / `vm1` / native 仍可能观察这些状态
- VM2 `tsload/tsrelease` 保持严格 barrier，JIT 不预取、不投机 materialize 瞬时字符串句柄
- VM2 只做函数级入口 JIT，不把 VM_func 入口退化成 native 直连逻辑

## 审计事件

- `jit_compile`
- `jit_trace_compile`
- `jit_invalidate`
- `jit_oom`
- `jit_fallback_backend`
- `vm2_jit_compile`
- `vm2_jit_invalidate`
- `vm2_jit_integrity_failure`
- `vm2_jit_oom`
- `vm2_jit_evict`
- `vm2_jit_fallback_backend`

统一为非扰动型日志事件。

## 环境变量 / 调试

- `VMP_JIT_BACKEND=off|c|x64`
- `VMP_JIT_VERBOSE=1`
- `VMP_JIT_CACHE_BUDGET=<bytes>`

`tools/vmp-vm1-run --jit=c|x64|off` 与 `tools/vmp-vm2-run --jit=c|x64|off` 都会写入 `VMP_JIT_BACKEND`；若 `VMP_JIT_VERBOSE=1`，启动时打印 `jit backend=X`。
