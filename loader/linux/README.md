# loader/linux

## 入口顺序
- `vmp_linux_init()` 使用 `__attribute__((constructor(101)))`
- 同时通过 `.init_array` fallback 指针注册，兼容较旧工具链
- 设计目标：晚于 libc、自身依赖已可用；早于默认优先级构造器（例如 `constructor(65535)`）

## 行为
- 若设置 `VMP_DISABLE_LOADER`：直接 no-op
- 审计路径优先级：`VMP_AUDIT_PATH` > `AuditWriter::default_path()`
- 记录 `loader_init` / `loader_init_failure`
- 初始化 `RuntimeState`
- 若存在 `VMP_STRING_MASTER_KEY`，恢复字符串密钥上下文并验证可派生
- 在初始化末尾调用一次 `vm_placeholder_analysis_awareness_hook()`

## 产物
- `libvmp_loader_linux.so`：可直接用于链接或 `LD_PRELOAD`
