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

### ci_fix_round_2
- 改动清单：
  - `tests/policy/policy_cpp_test.cpp`：移除 `/workspace/vmp/...` fixture 绝对路径；优先 `argv[1]`，其次 `VMP_POLICY_FIXTURES_DIR`，最后回退到 `CMAKE_CURRENT_SOURCE_DIR` 注入的默认目录。
  - `tests/policy/policy_cpp_summary.cpp`：支持通过相对文件名 + `VMP_POLICY_FIXTURES_DIR`/默认目录解析 policy fixture，不再依赖固定工作路径。
  - `tests/CMakeLists.txt`：在测试目录内 `find_package(Python3 REQUIRED COMPONENTS Interpreter)`；所有 ctest 中的 Python 脚本统一改为 `${Python3_EXECUTABLE}`；为 `policy_cpp_test` 注入 `VMP_POLICY_FIXTURES_DIR=${CMAKE_SOURCE_DIR}/tests/policy/examples`；为 policy/audit C++ helper 注入默认 fixture 目录宏。
  - `tests/audit/audit_cpp_test.cpp`、`tests/audit/compare_cpp_rust_audit.py`：移除 `/workspace/vmp` 绝对路径，改为默认 fixture 目录或由脚本位置推导 repo root。
  - `runtime/audit/rust_audit/tests/format_tests.rs`、`bindings/rust/vmp-policy/tests/cross_check.rs`：cargo 测试改为从 `CARGO_MANIFEST_DIR` 推导 repo root / fixture 路径，并自动搜索 repo 内现有 `build*` 目录下的 `audit_cpp_format` / `policy_cpp_summary` helper（也支持显式环境变量覆盖）。
  - `.gitignore`：新增 `vm_runtime_audit.log`、`*.log`、`.cargo/registry/`。
  - Git tree 清理：`git rm --cached vm_runtime_audit.log`，将误提交生成物移出版本控制。
- 变更文件：
  - `.gitignore`
  - `bindings/rust/vmp-policy/tests/cross_check.rs`
  - `runtime/audit/rust_audit/tests/format_tests.rs`
  - `tests/CMakeLists.txt`
  - `tests/audit/audit_cpp_test.cpp`
  - `tests/audit/compare_cpp_rust_audit.py`
  - `tests/policy/policy_cpp_summary.cpp`
  - `tests/policy/policy_cpp_test.cpp`
  - `vm_runtime_audit.log`（git index 移除）
- 验证结果（仓库原路径 `/workspace/vmp`）：
  - `cmake -S . -B build-ci-fix -G Ninja -DVMP_PLATFORM=linux -DVMP_ARCH=x64`：通过。
  - `cmake --build build-ci-fix -j`：通过。
  - `ctest --test-dir build-ci-fix --output-on-failure`：`9/9` 通过。
  - `cargo test --workspace`：通过；`rust_audit` 5 个测试、`vmp-policy` 2 个单元测试、`cross_check` 2 个测试全部通过。
- 验证结果（CI 模拟路径 `/tmp/vmp_ci_sim`）：
  - `cp -r /workspace/vmp /tmp/vmp_ci_sim && cd /tmp/vmp_ci_sim && rm -rf build-*` 后重跑 `cmake + build + ctest`：全部通过。
  - `ctest --test-dir build-ci-fix --output-on-failure`：`9/9` 通过，证明测试不依赖 `/workspace/vmp` 绝对路径。
- 未完成项：无（本轮要求范围内）。
- 下一子任务建议：等待 supervisor 指定下一轮 CI/跨平台修复项。

### subtask_04
- 本轮清单：
  - 实现 `bindings/cpp/include/vmp/bindings/cpp/annotate.h`，提供 zero-cost `VMP_VM_FUNC` / `VMP_VM_STRING` 头文件宏；Clang/GCC 走 `annotate("vmp_vm_*")`，MSVC C++ 走 `[[vmp::...]]`，MSVC C 保留源码标记供 fallback 扫描。
  - 实现 `bindings/cpp` C/C++ 标注前端：
    - `vmp-cpp-clang-collect`：基于 Clang AST 的采集工具，识别函数/变量上的 `annotate("vmp_vm_func")`、`annotate("vmp_vm_string")`，输出单个 Policy IR JSON。
    - `vmp_annotate_plugin`：FrontendAction/plugin 形态的 clang 插件构建产物（满足 plugin 模式交付）；驱动当前优先使用独立 AST collector，plugin 路径信息仍保留给 `-Xclang -load`/`VMP_PLUGIN_DIR`。
    - `vmp-cpp-fallback-scan`：无 LLVM/Clang dev 或 `VMP_DISABLE_CLANG_PLUGIN=1` 时启用的源码正则扫描器，覆盖 `VMP_VM_FUNC` / `VMP_VM_STRING` / `annotate(...)` / `[[vmp::...]]` 标记。
  - 实现 `tools/vmp-clang` / `tools/vmp-clang++`：兼容 `clang` 参数透传；带 `--vmp-collect=<policy.json>` 时先调用宿主编译，再运行 AST collector，失败时回落到 fallback scanner；无 `--vmp-collect` 时退化为纯透传。
  - 完成语义映射：
    - `VM_func` → `protection_domain=vm1`
    - `VM_string` → `sensitivity_level=highly_sensitive`、`plaintext_budget=transient_only`
    - 同时 `VM_func + VM_string` 的函数保持 `vm1` 执行域，并升级敏感数据保护
    - 变量声明、`const char[]`、`constexpr char[]`、`constexpr std::string_view` 初始化中的字符串字面量均能采集
    - `symbol_or_region` 在 AST collector 模式下输出 `mangled|display`，fallback 模式输出源码名或 `literal::<file>:<line>[ :<column> ]|"text"`
  - 完成 `tests/bindings_cpp/` 真测：C/C++ 样例、期望 JSON、JSON 语义比较脚本、fallback 强制禁用测试、clang/gcc 头文件编译测试、wrapper 透传测试。
  - 更新 `bindings/cpp/README.md`，补充宏、collector/plugin/fallback 机制、Policy IR 字段映射。
- 变更文件：
  - `CMakeLists.txt`
  - `STATUS.md`
  - `bindings/cpp/CMakeLists.txt`
  - `bindings/cpp/README.md`
  - `bindings/cpp/clang_plugin/vmp_annotate_plugin.cpp`
  - `bindings/cpp/clang_plugin/vmp_annotate_tool.cpp`
  - `bindings/cpp/include/vmp/bindings/cpp/annotate.h`
  - `bindings/cpp/include/vmp/bindings/cpp/plugin.h`
  - `bindings/cpp/src/fallback_scanner.cpp`
  - `bindings/cpp/src/fallback_scanner_main.cpp`
  - `bindings/cpp/src/plugin.cpp`
  - `policy/CMakeLists.txt`
  - `tests/CMakeLists.txt`
  - `tests/bindings_cpp/assert_exists.py`
  - `tests/bindings_cpp/compare_policy_json.py`
  - `tests/bindings_cpp/expected_c.json`
  - `tests/bindings_cpp/expected_cpp_fallback.json`
  - `tests/bindings_cpp/expected_cpp_plugin.json`
  - `tests/bindings_cpp/sample_c.c`
  - `tests/bindings_cpp/sample_cpp.cpp`
  - `tools/CMakeLists.txt`
  - `tools/src/vmp_clang.cpp`
  - `tools/src/vmp_clangxx.cpp`
  - `tools/src/vmp_driver_common.h`
- 未完成项：
  - 未做 LLVM backend 真实 IR 改写；本轮仅做采集与 JSON 产出。
  - 未做 Rust 过程宏、VM/JIT/字符串保护真实实现（按本轮范围未展开）。
  - `vmp_annotate_plugin` 当前作为构建交付物保留，但驱动默认优先走独立 AST collector；后续如需直接 `-Xclang -load` 生产链路，可继续把 plugin 路径接入外部构建系统。
- 下一子任务建议：
  - 进入 analyzer / planner 对接：消费本轮产出的 Policy IR entries，把 `VM_func`/`VM_string` 前端硬约束接到 Program IR / Protection Plan 最小闭环。
- 验证：
  - `cmake --build build -j`：通过。
  - `ctest --test-dir build --output-on-failure`：`20/20` 通过。
  - `cargo test --workspace`：通过。
  - `rg -n 'NOT_IMPLEMENTED' bindings/cpp`：无结果。
  - `/tmp/vmp_ci_sim` 中重跑 `cmake -S . -B build -G Ninja && cmake --build build -j && ctest --test-dir build --output-on-failure && cargo test --workspace`：全部通过。

### ci_fix_round_3
- 改动清单：
  - 仓库内移除 `.cargo/config.toml`（`git rm` 语义），避免 GitHub runner 被 Debian 本地 registry 路径 `/usr/share/cargo/registry` 绑死；容器内改走家目录级 `/root/.cargo/config.toml`，CI workflow 无需改动。
  - `.gitignore` 补齐/确认：`.cargo/`、`vm_runtime_audit.log`、`*.log`、`build-*/`、`target/`、`**/target/`；复检当前 git tracked 临时日志 / `build-*` 生成物，无新增需清理项。
  - `tests/CMakeLists.txt`：新增 `if(WIN32) set(VMP_TEST_BIN_SUFFIX ".exe") endif()`；所有 ctest 中被测二进制统一改为 `$<TARGET_FILE:...>` 传绝对路径，不再手写 `${CMAKE_BINARY_DIR}/tools/...` / `${CMAKE_BINARY_DIR}/tests/...`。
  - `tests/CMakeLists.txt`：`cross_language_golden` 现在显式把 `audit_cpp_format` 绝对路径与 `${CMAKE_SOURCE_DIR}` 传给 Python 驱动，并加 `SKIP_REGULAR_EXPRESSION "cross language audit golden SKIPPED"`。
  - `tests/audit/compare_cpp_rust_audit.py`：
    - 通过 `argv` 接收 C++ helper 绝对路径、fixture、repo root；
    - 先 `shutil.which("cargo")` 检查 cargo，可用性缺失时输出 skip 而非 error；
    - `cargo build -p rust_audit --example format_record` 后直接执行生成的 example 二进制（`.exe` / 非 `.exe` 都支持），不再 `cargo run`。
  - `tests/policy/compare_cpp_rust.py`：同样改为 cargo availability 检查 + `cargo build` + 直接执行 example，可避免 shell/路径差异。
- 变更文件：
  - `.cargo/config.toml`（删除，不再入仓）
  - `.gitignore`
  - `tests/CMakeLists.txt`
  - `tests/audit/compare_cpp_rust_audit.py`
  - `tests/policy/compare_cpp_rust.py`
  - `STATUS.md`
- 本地验证（仓库原路径 `/workspace/vmp`）：
  - `CARGO_NET_OFFLINE=1 cargo build -q -p rust_audit --example format_record`：通过；证明去掉仓库内 `.cargo/config.toml` 后，容器仍可通过家目录级 cargo config 离线构建。
  - `cmake -S . -B build-ci-fix3 -G Ninja -DVMP_PLATFORM=linux -DVMP_ARCH=x64`：通过。
  - `cmake --build build-ci-fix3 -j2`：通过。
  - `ctest --test-dir build-ci-fix3 --output-on-failure`：`20/20` 通过。
- `/tmp` 路径验证（CI 模拟目录 `/tmp/vmp_ci_sim_win`）：
  - 先复制仓库到 `/tmp/vmp_ci_sim_win`，再 `rm -rf build-* target`。
  - `cmake -S . -B build-multi -G 'Ninja Multi-Config' -DCMAKE_CONFIGURATION_TYPES=Release -DVMP_PLATFORM=linux -DVMP_ARCH=x64`：通过。
  - `cmake --build build-multi --config Release -j2`：通过。
  - `ctest --test-dir build-multi -C Release --output-on-failure`：`20/20` 通过。
- 如何保证 Windows 也能过：
  - 通过 `$<TARGET_FILE:vmp-protect>` / `$<TARGET_FILE:audit_cpp_format>` / `$<TARGET_FILE:vmp-clang>` 等 generator expression，CTest 生成时已解析为多配置真实路径；在 `/tmp/vmp_ci_sim_win/build-multi/tests/CTestTestfile.cmake` 中可见 `tools/Release/vmp-protect`、`tests/Release/audit_cpp_format`、`tools/Release/vmp-clang++` 等实际命令。
  - Python 驱动不再自行拼接后缀或相对路径，统一消费 CMake 传入的绝对路径 argv；因此 MSVC 的 `Release/*.exe` 路径也能被同一套驱动直接执行。
  - cargo 相关驱动已移除 `cargo run`，改成 `cargo build` 后直接 exec example，规避 Windows shell 解析差异。
- 未完成项：
  - 远端 GH Actions 结果尚待 supervisor 观察；若仍有 Windows-only 特例（例如 MSVC/clang-cl 工具链细节），进入下一轮再修。
- 下一子任务建议：
  - 等待 supervisor 回看最新 GH Actions；若 Windows runner 仍有特殊失败，优先抓取对应 job 的 CTest command line / stderr 做定点修复。

### subtask_05
- 本轮清单：
  - 实现 `runtime/vm1/` 的 VM1 ISA 定义：32 个通用寄存器、4 个浮点寄存器、`pc/sp/flags`、算术/位运算、显式宽度 load/store、控制流、`domain_call/domain_ret`、`load_transient_string`、模块头/常量池格式。
  - 实现 `Vm1Module` 加载/保存/序列化、文本 DSL 汇编器 `assemble_module_text()`、反汇编 `disassemble_module()`。
  - 实现 `Vm1Context` + `Vm1Interpreter`：私有线程栈（默认 64KiB）、寄存器快照式调用帧、溢出参数栈区、switch-in-loop dispatch、越界/除零/未知 opcode/stack overflow 等异常路径。
  - 实现 `runtime/vm1/include/vmp/runtime/bridge/bridge.h` 与 `BridgeRegistry`：`native↔vm1` / `vm1↔vm1` 最小跨域 ABI、`max_depth` 保护、异常状态映射与 `last_domain_exception()`。
  - 实现工具：`vmp-vm1-asm`、`vmp-vm1-run`。
  - 实现 audit 集成：`breakpoint -> vm1_breakpoint`、`trap -> vm1_trap`、`unknown opcode -> vm1_unknown_opcode`、`stack overflow -> vm1_stack_overflow`，统一 `audit_only`。
  - 新增 `tests/runtime_vm1/` 真测：round-trip、arith、control_flow、call/ret、cross-domain、exceptions、breakpoint、bench、CLI fib20。
  - 更新 `runtime/vm1/README.md`：ISA、字节码格式、DSL、跨域 ABI 文档。
- 本轮变更文件：
  - `runtime/vm1/CMakeLists.txt`
  - `runtime/vm1/README.md`
  - `runtime/vm1/include/vmp/runtime/bridge/bridge.h`
  - `runtime/vm1/include/vmp/runtime/vm1/isa.h`
  - `runtime/vm1/include/vmp/runtime/vm1/vm1.h`
  - `runtime/vm1/src/bridge.cpp`
  - `runtime/vm1/src/interpreter.cpp`
  - `runtime/vm1/src/vm1.cpp`
  - `tests/CMakeLists.txt`
  - `tests/runtime_vm1/test_common.h`
  - `tests/runtime_vm1/vm1_asm_round_trip.cpp`
  - `tests/runtime_vm1/vm1_arith_ops.cpp`
  - `tests/runtime_vm1/vm1_control_flow.cpp`
  - `tests/runtime_vm1/vm1_call_ret.cpp`
  - `tests/runtime_vm1/vm1_cross_domain_call.cpp`
  - `tests/runtime_vm1/vm1_exceptions.cpp`
  - `tests/runtime_vm1/vm1_breakpoint_event.cpp`
  - `tests/runtime_vm1/bench/vm1_bench.cpp`
  - `tests/runtime_vm1/fixtures/fib20.vm1s`
  - `tools/CMakeLists.txt`
  - `tools/src/vmp_vm1_asm.cpp`
  - `tools/src/vmp_vm1_run.cpp`
  - `STATUS.md`
- 未完成项：
  - 本轮要求范围内无未完成项。
  - `vm2` / JIT / 真字符串保护 / 更复杂对象句柄桥接仍留待后续指定子任务。
- 下一子任务建议：
  - 等待 supervisor 指定下一轮（建议后续对接 `VM_string` 真瞬时解密链或 VM2 独立 ISA/解释器子任务）。
- 验证：
  - `cd /workspace/vmp && cmake --build build -j`：通过。
  - `cd /workspace/vmp && ctest --test-dir build --output-on-failure`：`30/30` 通过。
  - `cd /workspace/vmp && cargo test --workspace`：通过。
  - `cd /workspace/vmp && ./build/tools/vmp-vm1-asm tests/runtime_vm1/fixtures/fib20.vm1s /tmp/fib20.vm1 && ./build/tools/vmp-vm1-run /tmp/fib20.vm1 20`：输出 `ret_int=6765 ret_float=0`。
  - `cd /tmp/vmp_ci_sim && ctest --test-dir build --output-on-failure && cargo test --workspace`：全部通过。

### ci_fix_round_4
- 本轮清单：
  - 从版本控制中移除误提交的 `build/` 目录（`git rm -rf build`），清掉被跟踪的 `CMakeCache.txt`、`CMakeFiles/`、`CTestTestfile.cmake`、Ninja 产物和测试二进制。
  - 扩充 `.gitignore`，同时覆盖 `build/` 与 `build-*/`，避免后续再次把默认构建目录提交进仓库。
  - 复检 `git ls-files` 中的生成物模式：`(^|/)build/`、`CMakeCache`、`CMakeFiles`、`_deps/`；确认仓库索引内已无同类误提交产物。
- 变更文件：
  - `.gitignore`
  - `STATUS.md`
  - `build/` 下全部已跟踪生成物（删除出索引）
- 验证：
  - `cd /workspace/vmp && git ls-files | grep -E '(^|/)build/|CMakeCache|CMakeFiles|_deps/'`：无输出。
  - `cd /workspace/vmp && rm -rf build && cmake -S . -B build -G Ninja -DVMP_PLATFORM=linux -DVMP_ARCH=x64`：通过，干净 configure 成功。
  - `cd /workspace/vmp && cmake --build build -j && ctest --test-dir build --output-on-failure`：通过，`30/30` 测试通过。
  - `cd /workspace/vmp && cargo test --workspace`：通过；Rust workspace 全绿。
  - `rm -rf /tmp/vmp_ci_sim_clean && cp -a /workspace/vmp/. /tmp/vmp_ci_sim_clean/ && cd /tmp/vmp_ci_sim_clean && rm -rf build && cmake -S . -B build -G Ninja -DVMP_PLATFORM=linux -DVMP_ARCH=x64 && cmake --build build -j && ctest --test-dir build --output-on-failure && cargo test --workspace`：通过；clean copy 下 configure/build/test 全绿。
- 未完成项：
  - 本轮要求范围内无未完成项。
- 下一子任务建议：
  - 等待 supervisor 检查 CI；若仍有平台特定失败，再按失败 job 日志做定点修复。
