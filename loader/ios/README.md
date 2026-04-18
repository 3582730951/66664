# loader/ios

- 对应 plan：§2.6（iOS）、§7、§12、§14、§16
- 入口：`__attribute__((constructor))`
- 当前状态：ready

## 行为
- `vmp_ios_init()` 在动态库/静态库装载阶段执行初始化
- 初始化时完成：审计 sink、`RuntimeState`、字符串主密钥恢复、`vm_placeholder_analysis_awareness_hook()`
- 文件保持纯 C++；不依赖 Objective-C / Swift runtime，便于在 Linux 主机测试路径逻辑

## 审计路径规则
优先级：
1. `VMP_AUDIT_PATH`
2. `VMP_IOS_DOCUMENTS_DIR`
3. `HOME/Documents/vm_runtime_audit.log`
4. 当前工作目录 `./vm_runtime_audit.log`
5. `/tmp/vm_runtime_audit.log`

## Capability gate
- 启动时尝试 `mmap(PROT_READ|PROT_WRITE)` + `mprotect(PROT_READ|PROT_EXEC)`
- iOS 常规 App Sandbox 中该步骤通常失败（`EPERM` / W^X 限制），此时写入 `jit_execmem_unavailable`
- 运行时据此拒绝 x64 emitter；在 iOS 平台且 execmem 不可用时，JIT 会强制走解释器路径

## xcodebuild / SwiftPM 集成
`Package.swift` 保留为轻量占位说明；当使用 CMake iOS 构建时，Xcode/SwiftPM 可通过链接 `build-ios/.../libvmp_loader_ios.a` 接入现有工程。完整 Swift/ObjC bridge 仍留待后续子任务实现。
