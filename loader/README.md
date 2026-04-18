# loader

- 对应 plan：§2.6、§15
- 范围：平台 loader / lifecycle hooks 真实初始化入口
- 当前状态：linux/windows/android/ios ready

## 初始化目标
在任何受保护代码运行前完成以下动作：
1. 初始化本地审计 sink
2. 接入运行时状态机单例
3. 恢复字符串主密钥上下文（若环境变量存在）
4. 调用一次 `vm_placeholder_analysis_awareness_hook()`

## 统一环境变量
- `VMP_STRING_MASTER_KEY`：十六进制主密钥，供字符串运行时派生子密钥
- `VMP_AUDIT_PATH`：覆盖默认审计路径
- `VMP_DISABLE_LOADER`：非空即禁用 loader（测试/宿主环境使用）
- `VMP_FORCE_JIT_CAPABILITY=disallow`：强制 capability gate 判定 execmem 不可用

## 平台入口
- Linux：`constructor(101)` + `.init_array` 显式 fallback
- Windows：TLS callback（`.CRT$XLB`）+ DllMain 风格桥接
- Android：`JNI_OnLoad` + `constructor(101)`
- iOS：`constructor` + capability gate
