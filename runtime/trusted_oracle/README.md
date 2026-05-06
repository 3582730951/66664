# runtime/trusted_oracle

`runtime/trusted_oracle/` 落地 subtask 36 的 **Trusted Oracle Infra**，给后续 detector / env-integrity 路径提供三类基础能力：

1. **Direct syscall wrappers**
   - Linux x64：`syscall` 指令（`src/syscall_linux_x64.asm.S`）
   - Linux / Android ARM64：`svc #0`（`src/syscall_linux_arm64.asm.S`）
   - Windows x64：运行时 Hell's Gate 风格扫描 `ntdll` 取 syscall number，再生成 `mov r10, rcx; mov eax, imm32; syscall; ret` stub（`src/syscall_windows_x64.cpp`）
   - iOS ARM64：`svc #0x80`（`src/syscall_ios_arm64.asm.S`）
   - 当前公开便捷封装覆盖：`open/read/close/ptrace/clock_gettime/getrandom/sigaction/prctl/gettid`

2. **API prologue baseline**
   - 每个受监控 API / region 采样前 32 字节
   - 双副本：常驻加密副本 + 定时（60s）重派生的 ephemeral 副本
   - 校验时 resident / ephemeral / current 三方都比对，任何分歧都会产生 `api_prologue_tampered`
   - 默认监控集会优先导入早期 bootstrap 采样到的：`open/read/close/ptrace/clock_gettime/getrandom/sigaction/prctl/pthread_create`

3. **k-of-n Cross-source voting + thread liveness**
   - `voting.h` 提供动态阈值 `KOfNVoter`：normal=2/3，elevated=n-1，high=n
   - `ptrace_attached`: `/proc/self/status` `TracerPid`（direct syscall）+ `ptrace(PTRACE_TRACEME)` 子进程自测 + SIGTRAP induction
   - `debug_registers_set`: 线程上下文 / ptrace PEEKUSER / SIGTRAP induction 三源投票
   - `time_source_suspicious`: `rdtsc/cntvct_el0` + direct `clock_gettime` + hardware/steady timer
   - `random_source_healthy`: direct `getrandom` + `rdrand/rdseed` + stack-canary mixing
   - `memory_maps_injected`: `/proc/self/maps` + `/proc/self/smaps` + `/proc/self/numa_maps`
   - detector 线程存活校验：线程内 direct `gettid` vs 父线程从 `/proc/self/task` 观察到的新 TID
   - 分歧事件统一审计为 `oracle_divergence`；线程不一致审计 `thread_creation_hijacked`

## 说明

- resident / ephemeral baseline 当前使用 `key_context_id` 派生的 AES-CTR 风格 XOR keystream 加密，并在 refresh 时先擦除旧副本再替换。
- 本轮只扩展 `runtime/trusted_oracle/*` 与必要的 audit 事件文本，不触碰 policy / planner / frontends / runtime state machine。
- iOS / Windows / ARM64 路径均有条件编译 guard；本轮 CI / regression 主要在 Linux x64 上验证。
