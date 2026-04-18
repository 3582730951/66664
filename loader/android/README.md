# loader/android

- 对应 plan：§2.6（Android）、§7、§12、§14、§16
- 入口：`JNI_OnLoad` + `__attribute__((constructor(101)))`
- 当前状态：ready

## 行为
- `JNI_OnLoad(JavaVM*, void*)` 会调用 `vmp_android_init()`，并返回 `JNI_VERSION_1_6`
- 非 JNI 装载路径仍会通过 `constructor(101)` 初始化
- 初始化时完成：审计 sink、`RuntimeState`、字符串主密钥恢复、`vm_placeholder_analysis_awareness_hook()`

## 审计路径规则
优先级：
1. `VMP_AUDIT_PATH`
2. `VMP_ANDROID_FILES_DIR`
3. `VMP_ANDROID_PACKAGE` 派生的 `/data/data/<pkg>/files/vm_runtime_audit.log`
4. 当前工作目录 `./vm_runtime_audit.log`
5. `/tmp/vm_runtime_audit.log`

## JIT capability gate
- 启动时探测匿名 RW 页能否转成 RX 页
- `VMP_FORCE_JIT_CAPABILITY=disallow` 可强制报告不可用（测试钩子）
- 若 execmem 不可用：写入 `jit_execmem_unavailable` 审计事件，并将运行时标记为 `jit_execmem_unavailable`
- `Vm1Jit` / `Vm2Jit` 会据此拒绝 x64 emitter，优先退到 portable C backend；若 C backend 也不可用，则静默回退到解释器

## Gradle / CMake 集成
`build.gradle.kts` 保留为占位入口，后续可通过 `externalNativeBuild { cmake { ... } }` 将 `libvmp_loader_android.so` 接入 AAR / APK 构建。当前仓库中的权威构建入口仍是 CMake/NDK。
