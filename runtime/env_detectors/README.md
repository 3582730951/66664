# runtime/env_detectors

`runtime/env_detectors/` 实现 subtask 31+32+33 的运行时环境检测与 lockstep 基座：

- `hardware_breakpoint_detected`
  - Linux/Android 优先走 child tracer + `PTRACE_PEEKUSER` 读取 `u_debugreg[0..7]`
  - iOS / 其他平台退化为 SIGTRAP 交叉校验
- `frida_injection_detected`
  - direct `/proc/self/maps` 关键字扫描：`frida-agent` / `frida-gadget` / `__frida`
  - TLS slot 异常（`PT_TLS` image 数量）
- `emulator_detected`
  - CPUID leaf `0x40000000`
  - `rdtsc` 抖动分布
  - Linux `arch_prctl(ARCH_GET_FS)` FSBASE 合法性
- `detector_heartbeat_drift`
  - 三个 detector 各自原子心跳
  - dispatcher 默认每 `2^12` 次调度检查一次，连续 3 次无前进即审计并走 delayed-exit

当前回归环境主要覆盖 Linux x64；其他平台路径保留统一 API 与编译期分支，待对应矩阵继续实机验证。
