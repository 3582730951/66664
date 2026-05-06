# runtime/stack_probe

随机栈回溯探测模块：

- 触发门限：`(counter.fetch_add(1) & 0xFFF) == token_low12`
- 主路径：frame-pointer walk（依赖全局 `-fno-omit-frame-pointer`）
- 兜底：优先 `libunwind`；若当前工具链未提供 `libunwind.h`，退化到 ABI unwind backtrace
- Linux / Android：通过 ST36 `DirectSyscall::open_readonly/read/close` 直读 `/proc/self/maps`
- 违例事件：`anon_executable_frame`
- 反应策略：`audit_then_delayed_exit`

默认注入点：

- `dispatcher_entry`
- `trampoline_target_prologue`
- `vm1_handler_dispatch`
- `vm2_handler_dispatch`
