# tools

- 对应 plan：§3
- 范围：vmp-clang / vmp-clang++ / vmp-link / vmp-protect / loader 自检工具
- 当前状态：minimal-runtime-ready

## 本轮补充
- `vmp-protect --platform <linux|windows|android|ios|macos>`：记录产物目标平台，并为后续 binary rewriter / loader 挂接保留入口（本轮仅文档/参数接线，不做重写）。
- `vmp-loader-selftest`：最小 loader 自检工具；通过构造期/TLS 回调验证 loader 是否已执行。
