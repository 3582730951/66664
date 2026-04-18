# STATUS

## 历史轮次：仓库骨架
- 完成 `/workspace/vmp/` 仓库骨架与 plan §2 六层、§3 对外接口、§10 平台/架构分层的目录布局。
- 完成顶层 CMake 构建骨架，提供 `VMP_WITH_JIT`、`VMP_WITH_VM2`、`VMP_PLATFORM`、`VMP_ARCH` 选项。
- 完成 Cargo workspace 骨架，覆盖 `bindings/rust/` 与 `runtime/` 下 Rust crates。
- 完成 Android `build.gradle.kts` 占位与 iOS `Package.swift` 占位。
- 完成 `tools/` 空入口与 `tests/` dummy CTest。

## 本轮清单（子任务 2：Policy IR）
- 完成 `policy/` C++ Policy IR：字段、枚举、`PolicyEntry` / `PolicyIR` / `ValidationError`、严格 JSON 读写、schema dump、验证器、`apply_vm_func_annotation()`、`apply_vm_string_annotation()`。
- 完成 `tools/vmp-protect` 最小闭环：支持 `--policy`、`--emit-policy-json`、`--dump-schema`、`--validate-only`，并输出人类可读错误。
- 新增 Rust crate `bindings/rust/vmp-policy`：`serde` 反序列化、同构 `PolicyIR`/`PolicyEntry`、`load_from_file()`、`validate()`。
- 将 `vmp-policy` 接入 Cargo workspace，并新增本地 cargo source 配置，使用 Debian 提供的 Rust crate 源离线构建。
- 完成 `tests/policy/` 真测：C++ round-trip、注解叠加语义、硬约束正反例、CLI 成功/失败路径、Rust/C++ 共享 JSON 解析结果等价。
- 更新 `policy/README.md` 与 `BUILD.md` 文档。

## 本轮变更文件（相对路径）
- `.cargo/config.toml`
- `BUILD.md`
- `Cargo.toml`
- `STATUS.md`
- `bindings/rust/vmp-policy/Cargo.toml`
- `bindings/rust/vmp-policy/examples/summary.rs`
- `bindings/rust/vmp-policy/src/lib.rs`
- `bindings/rust/vmp-policy/tests/cross_check.rs`
- `policy/CMakeLists.txt`
- `policy/README.md`
- `policy/include/vmp/policy/policy_ir.h`
- `policy/src/policy_ir.cpp`
- `tests/CMakeLists.txt`
- `tests/policy/assert_bad_policy.py`
- `tests/policy/policy_cpp_summary.cpp`
- `tests/policy/policy_cpp_test.cpp`
- `tests/policy/examples/good.json`
- `tests/policy/examples/good_ios_hot_only.json`
- `tests/policy/examples/bad_vm_func_native.json`
- `tests/policy/examples/bad_vm_string_sensitivity.json`
- `tests/policy/examples/bad_vm_string_plaintext_budget.json`
- `tests/policy/examples/bad_audit_event_type.json`
- `tests/policy/examples/bad_ios_aggressive.json`
- `tests/policy/examples/bad_vm2_integrity.json`
- `tools/CMakeLists.txt`
- `tools/src/vmp_protect.cpp`

## 验证命令与结果
1. 依赖安装：
   ```bash
   apt-get install -y nlohmann-json3-dev \
     librust-serde-dev librust-serde-derive-dev librust-serde-json-dev librust-tempfile-dev
   ```
   结果：成功。

2. CMake 构建：
   ```bash
   cmake -S /workspace/vmp -B /workspace/vmp/build-linux-x64 -DVMP_PLATFORM=linux -DVMP_ARCH=x64
   cmake --build /workspace/vmp/build-linux-x64 -j
   ```
   结果：成功。

3. CTest：
   ```bash
   ctest --test-dir /workspace/vmp/build-linux-x64 --output-on-failure
   ```
   结果：6/6 通过。

4. Cargo workspace：
   ```bash
   cd /workspace/vmp && cargo test --workspace
   ```
   结果：全部通过；`vmp-policy` 单元测试与跨语言等价测试通过。

5. CLI 正例：
   ```bash
   /workspace/vmp/build-linux-x64/tools/vmp-protect --policy /workspace/vmp/tests/policy/examples/good.json
   ```
   实际输出：`OK: policy loaded, 2 entries, schema=v1`
   退出码：`0`

6. CLI 反例：
   ```bash
   /workspace/vmp/build-linux-x64/tools/vmp-protect --policy /workspace/vmp/tests/policy/examples/bad_vm_func_native.json
   ```
   实际输出包含：`error[vm_func_native] ...`
   退出码：`2`

## 本轮未实现项
- `vmp-protect` 的实际保护流程（除 policy 载入/验证/导出/schema 以外）仍未实现，当前仍可打印 `NOT_IMPLEMENTED`。
- `analyzer/`：源码级/二进制级分析逻辑未实现。
- `planner/`：Protection Plan 决策逻辑未实现。
- `backends/llvm/`：LLVM lifting / pass / 插桩未实现。
- `backends/rewriter/`：PE/ELF/Mach-O/APK/IPA 重写未实现。
- `runtime/*`、`loader/*`、`arch/*` 真实 VM/JIT/字符串保护/审计/完整性/平台接入未实现。
- `bindings/cpp/` attribute plugin/front-end 集成未实现；`bindings/rust/vmp-macros` 仍是透传占位。

## 下一子任务建议
- 进入 `analyzer/` + `planner/` 最小闭环：在已有 Policy IR 基础上补 `ProgramIR` / `ProtectionPlan` 最小类型与空规划流，使 `vmp-protect --policy ... <input>` 可以在不做真实保护的前提下走通“读策略 → 产出空计划”路径。

### ci_fix_round_1
- 改动清单：
  - `CMakeLists.txt`：在 options 之后、`add_subdirectory(...)` 之前引入 `cmake/third_party.cmake`，并统一调用 `vmp_require_nlohmann_json()`。
  - `cmake/third_party.cmake`：新增第三方依赖入口，优先 `find_package(nlohmann_json CONFIG QUIET)`，失败后使用 `FetchContent_MakeAvailable` 从 `https://github.com/nlohmann/json.git` 的 `v3.11.3` 拉取。
  - `policy/CMakeLists.txt`：删除 `find_package(nlohmann_json REQUIRED)`，保留 `target_link_libraries(vmp_policy PUBLIC nlohmann_json::nlohmann_json)`。
- 两次 configure 结果：
  - `cmake -S /workspace/vmp -B /workspace/vmp/build-fix`：成功；命中系统已安装的 `nlohmann_json` 包路径。
  - `cmake -S /workspace/vmp -B /workspace/vmp/build-fix-fetch -DCMAKE_DISABLE_FIND_PACKAGE_nlohmann_json=TRUE`：成功；命中 `FetchContent` 分支并完成 `v3.11.3` 拉取。
- 其他本轮验证：
  - `cmake --build /workspace/vmp/build-fix -j`：成功。
  - `ctest --test-dir /workspace/vmp/build-fix --output-on-failure`：成功，`6/6` 通过。
  - `cargo test --workspace`：成功。
- 未完成项：无（本轮子任务范围内）。
- 下一子任务建议：等待 supervisor 指定下一轮 CI/跨平台修复项。

### subtask_03
- 本轮清单：
  - 实现 `runtime/audit/` C++ 审计运行时：`AnalysisEventRecord`、`make_event(...)`、结构化单行格式化、append-only `AuditWriter`、`IDetector`/`NullDetector`、`ReactionDispatcher`、占位 hook 初始化。
  - 实现 `runtime/audit/rust_audit/` Rust 镜像：同构 `AnalysisEventRecord + serde`、逐字节等价 `format_line()`、`AuditWriter`、`ReactionDispatcher`、可注入 delayed-exit hook、`format_record` example。
  - 将 `vmp-protect` 扩展为支持 `--detector-selftest`：用 `NullDetector` 注入 3 条假事件，经 `audit_then_delayed_exit` 完整走通审计 + delayed-exit hook（测试下注入 flag，不真实退出）。
  - 新增 `tests/audit/`：C++ 精确格式/失败静默/并发写入/策略分发/null detector/placeholder，Rust 精确格式与跨语言 golden，CLI selftest 校验日志 3 行。
  - 更新 `runtime/audit/README.md`：补充行格式 EBNF、字段语义、默认路径、线程安全与失败静默策略。
- 变更文件：
  - `runtime/audit/CMakeLists.txt`
  - `runtime/audit/README.md`
  - `runtime/audit/include/vmp/runtime/audit/audit.h`
  - `runtime/audit/include/vmp/runtime/audit/detector.h`
  - `runtime/audit/include/vmp/runtime/audit/placeholder.h`
  - `runtime/audit/include/vmp/runtime/audit/reaction.h`
  - `runtime/audit/src/audit.cpp`
  - `runtime/audit/src/detector.cpp`
  - `runtime/audit/src/placeholder.cpp`
  - `runtime/audit/src/reaction.cpp`
  - `runtime/audit/rust_audit/Cargo.toml`
  - `runtime/audit/rust_audit/examples/format_record.rs`
  - `runtime/audit/rust_audit/src/lib.rs`
  - `runtime/audit/rust_audit/tests/format_tests.rs`
  - `tests/CMakeLists.txt`
  - `tests/audit/assert_detector_selftest.py`
  - `tests/audit/audit_cpp_format.cpp`
  - `tests/audit/audit_cpp_test.cpp`
  - `tests/audit/compare_cpp_rust_audit.py`
  - `tests/audit/golden_line.txt`
  - `tests/audit/golden_record.json`
  - `tools/CMakeLists.txt`
  - `tools/src/vmp_protect.cpp`
  - `STATUS.md`
- 未完成项：
  - `ReactionPolicy::{log,degrade,decoy_terminate}` 仅保留 enum 与 dispatcher 分支/TODO；详细状态机仍待后续子任务。
  - 真 detector（硬件断点、完整性异常、maps 篡改等）尚未实现；本轮只交付接口与 `NullDetector`。
  - Android/iOS loader 真入口未接入；`default_path()` 仅保留平台骨架与环境变量注入点。
- 下一子任务建议：
  - 进入 state machine / 真 detector 子任务：将 `log/degrade/decoy_terminate` 细化为可验证反应路径，并把后续硬件断点 detector 接到当前 audit sink 上，继续遵守 owner override 的 `hw_breakpoint => audit_then_delayed_exit`。
- 验证：
  - `cmake --build build-linux-x64 -j`：通过。
  - `cd build-linux-x64 && ctest --output-on-failure`：`9/9` 全绿。
  - `cargo test --workspace`：全绿；`rust_audit` 5 个测试通过。
  - `./build-linux-x64/tools/vmp-protect --detector-selftest`：退出码 `0`，输出 `audit:ok exits_triggered=3`。
  - `./vm_runtime_audit.log`：共 `3` 行，且 `hw_breakpoint` / `integrity_mismatch` / `unknown` 各 `1` 行。
