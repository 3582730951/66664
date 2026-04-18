# loader/windows

## 入口顺序
- TLS callback 指针放入 `.CRT$XLB`
- 进程附加时通过 TLS callback 触发一次 `init_once_process()`
- 同时导出 `vmp_windows_loader_dll_main(...)`，供宿主 `DllMain` / 静态链接场景显式桥接

## 行为
- 若设置 `VMP_DISABLE_LOADER`：直接 no-op
- 审计路径优先级：`VMP_AUDIT_PATH` > `AuditWriter::default_path()`
- 记录 `loader_init` / `loader_init_failure`
- 初始化 `RuntimeState`
- 若存在 `VMP_STRING_MASTER_KEY`，恢复字符串密钥上下文并验证可派生
- 初始化末尾调用一次 `vm_placeholder_analysis_awareness_hook()`
- 线程 attach/detach 事件通过 TLS callback 维护最小线程本地状态

## 节名
- MSVC：`#pragma section(".CRT$XLB", long, read)` + `__declspec(allocate(...))`
- MinGW/clang：`__attribute__((section(".CRT$XLB"), used))`
