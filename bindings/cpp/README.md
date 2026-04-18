# bindings/cpp

对应 plan：§1、§2.1、§4、§5、§9。

## 头文件宏

头文件：`bindings/cpp/include/vmp/bindings/cpp/annotate.h`

- `VMP_VM_FUNC`
  - Clang/GCC: `__attribute__((annotate("vmp_vm_func")))`
  - MSVC C++: `[[vmp::vm_func]]`
  - MSVC C: 空宏 + 源码标记，交给 fallback scanner
- `VMP_VM_STRING`
  - Clang/GCC: `__attribute__((annotate("vmp_vm_string")))`
  - MSVC C++: `[[vmp::vm_string]]`
  - MSVC C: 空宏 + 源码标记，交给 fallback scanner

这些宏不生成额外代码，也不改变目标程序语义。

可用于：
- 函数声明 / 定义
- `const char[]`
- `constexpr char[]`
- `constexpr std::string_view`
- 其他变量声明（尤其是敏感常量、配置、密钥）

## clang plugin 采集

当 `find_package(LLVM)` 与 `find_package(Clang)` 成功时，会构建共享库：

- `vmp_annotate_plugin`

插件名：`vmp-collect`

关键参数：

- `-Xclang -load -Xclang <plugin-path>`
- `-Xclang -plugin -Xclang vmp-collect`
- `-Xclang -plugin-arg-vmp-collect -Xclang -policy-out=<path>`

插件只做采集，不做 IR 改写。

## fallback 机制

总会构建一个简易源码扫描器：

- `vmp-cpp-fallback-scan`

当以下任一条件成立时，`vmp-clang` / `vmp-clang++` 会自动改用 fallback：

- 未找到 LLVM/Clang dev headers，因此没有构建 plugin
- 设置 `VMP_DISABLE_CLANG_PLUGIN=1`
- plugin 加载或执行失败

fallback 直接扫描源码中的 `VMP_VM_FUNC` / `VMP_VM_STRING` / `annotate("vmp_vm_*")` / `[[vmp::...]]` 标记，因此在非 Clang 环境仍能产出 Policy IR，但符号字段通常只有源码名，没有 mangled name。

## Policy IR 字段映射

- `VM_func` → `protection_domain=vm1`
- `VM_string` → `sensitivity_level=highly_sensitive`, `plaintext_budget=transient_only`
- 同时 `VM_func + VM_string` 的函数：
  - 执行域保持 `vm1`（后续 planner 可提升到 `vm2`）
  - 数据保护升级到 `highly_sensitive`
- `language_origin`: 由源文件扩展名 / clang AST 语言模式决定（`c` 或 `cpp`）
- `annotation_origin`: plugin 路径默认 `attribute`；fallback 命中 pragma 形式时记为 `pragma`
- `symbol_or_region`:
  - plugin 模式优先输出 `mangled|display`
  - fallback 模式输出源码名或 `literal::<file>:<line>|"text"`

## 驱动

- `tools/vmp-clang`
- `tools/vmp-clang++`

用法：

```bash
vmp-clang [clang-args...] --vmp-collect=policy.json source.c
vmp-clang++ [clang-args...] --vmp-collect=policy.json source.cpp
```

未传 `--vmp-collect` 时，驱动退化为普通 `clang` / `clang++` 透传。
