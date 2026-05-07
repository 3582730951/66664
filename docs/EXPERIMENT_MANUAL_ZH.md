# PAVMP JSS 实验证据手册（中文）

版本：2026-05-07

适用范围：本手册用于补强 JSS/JSSS 审稿人可能关注的非人类评估证据链。人类逆向评估由 owner 单独执行；本手册只规定除人类评估以外的主流保护器对比、server-scale case、跨平台 live evidence、artifact 一键复现与证据归档流程。

## 0. 总原则

所有实验必须遵守以下规则：

1. 只记录真实执行过的结果。
2. 失败项必须进入 `skipped_checks`、`limitations` 或单独的失败报告，不能伪装成通过。
3. Positive-control fixture 只能声明为 fixture-level evidence，不能包装成 protected PE 或 Android binary 的真实 live attack evidence。
4. 主流保护器 baseline 只能使用合法获得的软件和配置。Tigress 下载如需姓名、机构、邮箱或许可证流程，不允许伪造身份信息。
5. 每个 claim 必须绑定 report 路径、artifact 路径、sha256、命令、runner metadata、版本号。
6. 论文正文只能采用已通过 validator 或明确人工复核的 report；草稿、失败日志、smoke report 不进入 paper claim。
7. 默认 artifact 的一键复现目标是“生成证据包并验证 claim 边界”，不是自动扩大论文 claim。

## 1. 当前证据基线

当前可引用的已完成证据如下：

| 证据项 | 当前状态 | 可声明内容 | 不可声明内容 |
|---|---|---|---|
| Windows external-live | run `25479114334`，commit `15ba50d43020af61a02924b26ee53becd5369306`，artifact `/tmp/windows-live-evidence-25479114334` validator 通过 | Windows C/C++ protected PE native execution 与 Debug API launch survival；debugger/HWBP/Frida detector positive-control fixture audit events | Protected PE live Frida attach 通过；真实外部硬件断点攻击已覆盖 |
| SQLite case study | `reports/sqlite_case_study_20260506.json`，40 runs，`sqlite3_step`，`overhead_ratio=1.01`，`all_correct=true` | 真实 SQLite 组件级 sanity check | DBMS service、browser、production deployment |
| Server case study | `reports/server_case_study_20260507.json`，20 runs，400 requests/run，concurrency 16，`all_correct=true` | 小型 loopback HTTP server 的 server-scale sanity check | 生产级多进程服务、互联网部署、主流保护器对比 |
| Artifact reproduction | `paper/artifact/reproduce.sh`，`paper/tests/validate_paper.py` | 一键生成 paper-facing reports/PDF 的主路径 | 外部 Windows/Android/Frida 真机实验自动完成 |
| 5-agent review | commit `15ba50d` 后 5 agents x 3 rounds 一致 | 当前通过概率不低于 80%，真实区间约 84%-88% | 当前材料足以诚实声明 90% |

## 2. 环境准备

推荐基础环境：

```bash
cd /workspace/vmp

apt-get update
apt-get install -y \
  build-essential cmake ninja-build clang lld gcc g++ \
  python3 python3-pip python3-venv jq git curl unzip \
  sqlite3 libsqlite3-dev \
  binutils gdb file time
```

如果需要 Docker external-live：

```bash
docker version
docker info
```

如果需要 Windows CI evidence：

```bash
git remote -v
curl -fsSL https://api.github.com/repos/3582730951/66664/actions/workflows/windows_live_evidence.yml
```

如果需要 Android 真机：

```bash
adb devices
adb shell getprop ro.product.cpu.abi
adb shell getprop ro.build.version.release
```

## 3. Artifact 一键复现

目标：证明 supplementary artifact 可以一键生成 paper-facing reports、dataset、PDF，并验证 claim 边界。

命令：

```bash
cd /workspace/vmp
paper/artifact/reproduce.sh
```

通过标准：

```text
paper validation OK
```

额外验证：

```bash
python3 paper/tests/validate_paper.py
python3 paper/scripts/validate_external_live.py
python3 -m json.tool paper/data/derived_metrics.json >/tmp/derived_metrics.checked.json
```

记录项：

| 字段 | 内容 |
|---|---|
| report/log | `reports/full_external_experiment_*.log` 或 shell transcript |
| commit | `git rev-parse HEAD` |
| dirty tree | `git status --short` |
| Docker | `docker version`，如使用 Docker |
| PDF | `paper/main.pdf` 或 TJSS package PDF |
| validator | `paper/tests/validate_paper.py` 输出 |

失败处理：

如果依赖缺失，记录为 artifact environment failure，不得写成 claim failure。修复依赖后重跑；不能只手工改 JSON。

## 4. Server-scale case study

目标：补强“SQLite 只是 component-scale”的风险，加入一个真实 TCP loopback 小型 HTTP server case。该实验保护 server 的 request classification 核心函数 `protected_route_score`，使用真实 socket 请求测吞吐和延迟分布。

脚本：

```bash
python3 paper/scripts/run_server_case_study.py --help
```

推荐正式运行：

```bash
cd /workspace/vmp
python3 paper/scripts/run_server_case_study.py \
  --runs 20 \
  --requests 400 \
  --concurrency 16 \
  --raw-dir reports/server_case_study_20260507 \
  --output reports/server_case_study_20260507.json
```

当前实测结果：

```text
server case study report: reports/server_case_study_20260507.json
{"latency_ratio": 1.0062, "ok": true, "runs": 20, "throughput_ratio": 1.0076}
```

当前 report 摘要：

| 字段 | 值 |
|---|---|
| report | `reports/server_case_study_20260507.json` |
| protected symbol | `protected_route_score` |
| runs | 20 |
| requests/run | 400 |
| concurrency | 16 |
| correctness | `all_correct=true` |
| throughput ratio | `1.0076` protected/baseline |
| median latency ratio | `1.0062` protected/baseline |

复核命令：

```bash
python3 -m json.tool reports/server_case_study_20260507.json >/tmp/server_case_study.checked.json
python3 - <<'PY'
import json
d=json.load(open("reports/server_case_study_20260507.json"))
assert d["schema"] == "pavmp.server_case_study.v1"
assert d["fabricated_data"] is False
assert d["all_correct"] is True
print(d["protected_symbol"], d["runs_per_variant"], d["requests_per_run"], d["concurrency"])
print(d["throughput_ratio_protected_over_baseline"], d["median_latency_ratio_protected_over_baseline"])
PY
```

论文可声明内容：

```text
We add a server-scale loopback HTTP case study that protects a request-classification function and measures throughput and latency over real TCP requests. The protected and baseline responses are identical over 20 runs x 400 requests at concurrency 16; the observed median throughput and latency ratios are approximately 1.01x.
```

必须保留的限制：

```text
This is not a production multi-process deployment, not an internet-facing service benchmark, and not a mainstream-protector comparison.
```

## 5. SQLite component case study

目标：保留当前 SQLite evidence，作为真实组件级数据库库函数案例。

运行：

```bash
cd /workspace/vmp
python3 paper/scripts/run_sqlite_case_study.py \
  --runs 40 \
  --iterations 5000 \
  --raw-dir reports/sqlite_case_study_20260506 \
  --output reports/sqlite_case_study_20260506.json
```

当前结果：

| 字段 | 值 |
|---|---|
| report | `reports/sqlite_case_study_20260506.json` |
| SQLite version | 3.40.1 |
| protected symbol | `sqlite3_step` |
| runs | 40 |
| iterations | 5000 |
| correctness | `all_correct=true` |
| overhead ratio | `1.01` |

复核：

```bash
python3 -m json.tool reports/sqlite_case_study_20260506.json >/tmp/sqlite_case_study.checked.json
python3 - <<'PY'
import json
d=json.load(open("reports/sqlite_case_study_20260506.json"))
assert d["all_correct"] is True
print(d["protected_symbol"], d["runs_per_variant"], d["overhead_ratio"])
PY
```

论文定位：

SQLite 是 component-scale evidence。它可以降低 toy benchmark 风险，但不能替代 server-scale 或 full-system deployment evidence。

## 6. 主流保护器同负载 baseline

目标：回应“只和自己比”的竞争有效性问题。最低目标是加入 Tigress virtualization mode 或一个合法可复现开源保护器配置，在同一 workload、同一 correctness oracle、相同运行次数下比较。

### 6.1 基线选择优先级

优先级：

1. Tigress virtualization mode，前提是合法获得 binary 和许可证允许实验。
2. 可复现开源保护器配置，前提是能保护同一函数或可证明等价的函数集合。
3. 如果两者都不能合法运行，必须写 `NOT_AVAILABLE` / `skipped`，并记录阻断原因，不能伪造。

当前阻断：

Tigress 官方下载流程要求填写个人/机构/邮箱并进入许可证流程。本仓库自动化环境未合法获得 Tigress binary，因此当前不能把 Tigress baseline 计入真实 evidence。

### 6.2 同负载要求

至少选择两个 workload：

| Workload | 必选原因 | Oracle |
|---|---|---|
| `server_case` | server-scale，回应 systems 质疑 | HTTP response body hash、stdout aggregate、throughput/latency |
| `sqlite_case` 或 `bench_c` | 和现有数据对齐 | stdout parsed result、elapsed distribution |

如果 protector 只支持 C source-to-source，优先使用：

```text
reports/server_case_study_20260507/server_case.c
reports/sqlite_case_study_20260506/sqlite_case.c
tests/integration_targets/bench_c.c
```

### 6.3 Tigress 运行模板

前提：

```bash
command -v tigress
tigress --version
```

示例模板，需根据本地 Tigress 版本调整：

```bash
mkdir -p reports/protector_baseline_tigress_YYYYMMDD/server_case

tigress \
  --Environment=x86_64:Linux:Gcc:12.0 \
  --Seed=17 \
  --Transform=Virtualize \
  --Functions=protected_route_score \
  --out=reports/protector_baseline_tigress_YYYYMMDD/server_case/server_case.tigress.c \
  reports/server_case_study_20260507/server_case.c

gcc -O2 -g -fno-pie -no-pie -rdynamic \
  -I bindings/cpp/include \
  -o reports/protector_baseline_tigress_YYYYMMDD/server_case/server_case.tigress \
  reports/protector_baseline_tigress_YYYYMMDD/server_case/server_case.tigress.c \
  tests/integration_targets/trampoline_dispatch_elf.c
```

注意：

如果 Tigress 生成代码与 helper 或 annotation header 冲突，应创建最小等价 C source：保留同一 `protected_route_score` 逻辑、同一路径集合、同一 socket harness、同一 oracle。该变更必须记录为 `source_equivalence_note`，并保存 diff。

### 6.4 Baseline report schema

建议输出：

```json
{
  "schema": "pavmp.protector_baseline.v1",
  "generated_utc": "YYYY-MM-DDTHH:MM:SSZ",
  "fabricated_data": false,
  "protector": {
    "name": "Tigress",
    "version": "exact version",
    "configuration": "Virtualize protected_route_score, seed=17",
    "license_status": "legally obtained / not available"
  },
  "workload": {
    "name": "server_case",
    "source": "relative/path",
    "protected_function": "protected_route_score",
    "oracle": "HTTP body sha256 + stdout aggregate"
  },
  "runs_per_variant": 20,
  "requests_per_run": 400,
  "concurrency": 16,
  "baseline": {},
  "pavmp": {},
  "protector": {},
  "all_correct": true,
  "limitations": [
    "Single protector version and one configuration.",
    "Source-to-source protector baseline is not identical to binary rewriting."
  ]
}
```

### 6.5 通过标准

主流保护器 baseline 可进入论文的最低标准：

1. 合法获得 protector。
2. 能保护同一 workload 的同一函数，或有等价性说明。
3. Baseline/native、PAVMP、Protector 三者 output oracle 全部一致。
4. 每个 variant 至少 20 runs。
5. 记录 median/p95/p99 latency、throughput、binary size、strings/static anchors、失败项。
6. 报告中明确 non-claims，不做“全面胜过主流保护器”的泛化结论。

## 7. Linux external live campaign

目标：保留并刷新 Linux 上 Frida、ptrace/gdb、Qiling、preload sentinel 的 live evidence。

Docker 路径：

```bash
cd /workspace/vmp
PAVMP_EXTERNAL_LIVE_REBUILD=1 bash paper/artifact/external_live_docker.sh \
  --check frida \
  --check ptrace-gdb \
  --check qiling \
  --validate
```

完整路径：

```bash
PAVMP_EXTERNAL_LIVE_REBUILD=1 bash paper/artifact/run_full_external_experiment.sh
```

裸机路径：

```bash
python3 paper/scripts/run_external_live.py \
  --check frida \
  --check ptrace-gdb \
  --check qiling \
  --validate
```

验证：

```bash
python3 paper/scripts/validate_external_live.py reports/external_live_matrix_YYYYMMDD.json
```

失败处理：

如果 ptrace 被系统策略阻止，记录 `ptrace_or_debug_policy`，该 check 进入 skipped。不要用 preload sentinel 替代真实 Frida/gdb claim。

## 8. Windows evidence

目标：区分 runner proof、positive-control fixture、protected PE live attach 三类证据。

### 8.1 GitHub Actions Windows-live

当前已通过：

| 字段 | 值 |
|---|---|
| run | `25479114334` |
| commit | `15ba50d43020af61a02924b26ee53becd5369306` |
| artifact | `/tmp/windows-live-evidence-25479114334` |
| validator | OK |
| provenance | JSON 内嵌 run id / commit / workflow URL |

下载 artifact 后验证：

```bash
python3 paper/scripts/validate_external_live.py \
  --bundle-root /tmp/windows-live-evidence-25479114334 \
  /tmp/windows-live-evidence-25479114334/external_live_matrix_windows_target_c_25479114334.json \
  /tmp/windows-live-evidence-25479114334/external_live_matrix_windows_target_cpp_25479114334.json
```

检查 JSON：

```bash
python3 - <<'PY'
import json, pathlib
root=pathlib.Path("/tmp/windows-live-evidence-25479114334")
for p in sorted(root.glob("external_live_matrix_windows_*.json")):
    d=json.load(open(p))
    print(p.name, d["ok"], d.get("ci_provenance", {}).get("run_id"))
    print([(c["id"], c["ok"], c["audit_events"]) for c in d["checks"]])
    print("skipped", d.get("skipped_checks", []))
PY
```

### 8.2 Windows 真机 live Frida/Debug evidence

目标：补齐当前 `windows_frida_attach` skipped 的短板。

前提：

1. Windows 10/11 x64 真机或受控 VM。
2. Python 3.11。
3. Frida tools 可用。
4. 本仓库 checkout。
5. 使用长运行 protected PE harness，避免进程在 Frida attach 前退出。

当前仓库 CI target 过快退出，Frida attach 在 GitHub runner 上失败：

```text
windows_frida_attach skipped
VirtualAllocEx returned 0x00000005 或 attach before process exit failure
```

推荐做法：

1. 新增长运行 target，例如 `target_c_server` 或 `target_c_sleep_loop`。
2. 保护目标函数。
3. 启动 target，确认 PID。
4. 用 Frida attach。
5. 检查 audit log 是否有 `frida_injection_detected`。
6. 用 validator 检查 external-live JSON。

手工命令模板：

```powershell
python -m pip install --upgrade pip
python -m pip install frida-tools

cmake -S . -B build-windows-live -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DVMP_PLATFORM=windows `
  -DVMP_ARCH=x64

cmake --build build-windows-live --target `
  vmp-protect `
  vmp-trampoline-inject `
  windows_debugger_detector_fixture `
  env_detectors_hardware_breakpoint_cross_check `
  env_detectors_frida_divergence `
  -j

python tests/integration_targets/run_integration_ci.py `
  --build-dir build-windows-live `
  --report-dir reports `
  --platform-filter x86_64-windows `
  --test-filter target_c `
  --test-filter target_cpp

python tests/live_tool_campaign/run_windows_ci_live.py `
  --report-dir reports `
  --fixture-dir build-windows-live/tests `
  --default-test target_c `
  --frida `
  --output reports/external_live_matrix_windows_target_c_MANUAL.json

python paper/scripts/validate_external_live.py `
  --bundle-root reports `
  reports/external_live_matrix_windows_target_c_MANUAL.json
```

如果 `windows_frida_attach` 仍 skipped，只能声明 fixture evidence，不得声明 protected PE live Frida attach。

## 9. Android evidence

目标：将 Android 从 runner proof 提升到真机或受控 emulator live evidence。

### 9.1 Runner proof

当前可用入口：

```bash
python3 tests/integration_targets/run_integration_ci.py \
  --build-dir build \
  --report-dir reports \
  --platform-filter aarch64-android
```

该路径主要支撑 build/run correctness，不等同 Frida/emulator detection security claim。

### 9.2 Android 真机 Frida trace

前提：

1. arm64 Android device。
2. `adb devices` 可见设备。
3. 设备 root 或可运行 frida-server。
4. protected Android binary 可推送到 `/data/local/tmp/pavmp/`。

模板：

```bash
adb shell 'mkdir -p /data/local/tmp/pavmp'
adb push reports/integration_artifacts_YYYYMMDD/aarch64-android/target_c/target_c.protected /data/local/tmp/pavmp/target_c.protected
adb shell 'chmod 755 /data/local/tmp/pavmp/target_c.protected'

adb push frida-server /data/local/tmp/pavmp/frida-server
adb shell 'chmod 755 /data/local/tmp/pavmp/frida-server'
adb shell 'su -c /data/local/tmp/pavmp/frida-server &' || true

adb shell 'VMP_AUDIT_PATH=/data/local/tmp/pavmp/audit.log /data/local/tmp/pavmp/target_c.protected 100000'
adb pull /data/local/tmp/pavmp/audit.log reports/android_live_audit_target_c.log
```

Frida attach 模板：

```bash
frida-ps -U
frida -U -n target_c.protected -l external_scripts/noop_frida_probe.js
```

通过标准：

1. 目标输出与 oracle 一致，或出现预声明 configured reaction。
2. Audit log 包含 `frida_injection_detected` 或明确的 detector event。
3. external-live JSON 设置 `fabricated_data=false`。
4. `validate_external_live.py` 通过。

失败处理：

如果设备不能 root 或 frida-server 不能运行，记录为 skipped。不要用 Android qemu runner proof 替代真机 Frida evidence。

## 10. Windows/Android external-live JSON 手工记录

如果 collector 尚不支持某个真机路径，可以用模板生成 JSON，但必须包含真实 artifacts 和 sha256。

最小检查：

```bash
python3 paper/scripts/validate_external_live.py reports/external_live_matrix_ANDROID_OR_WINDOWS_YYYYMMDD.json
```

每个 check 必须包含：

| 字段 | 要求 |
|---|---|
| `ok` | 只有真实通过才为 true |
| `oracle_triggered` | 有 detector/reaction event 才为 true |
| `audit_events` | 必须出现在 `audit_excerpt` 中 |
| `evidence_artifacts` | 每个文件必须存在且 sha256 匹配 |
| `claim_scope` | 只描述该 runner/该 target/该工具 |
| `non_claims` | 明确排除未测平台和未测攻击 |
| `limitations` | 至少包含 runner、版本、样本规模 |

## 11. Artifact 打包检查

提交 supplementary 前执行：

```bash
cd /workspace/vmp
paper/artifact/reproduce.sh
python3 paper/tests/validate_paper.py
python3 paper/scripts/validate_external_live.py --bundle-root /tmp/windows-live-evidence-25479114334 \
  /tmp/windows-live-evidence-25479114334/external_live_matrix_windows_target_c_25479114334.json \
  /tmp/windows-live-evidence-25479114334/external_live_matrix_windows_target_cpp_25479114334.json
```

检查敏感信息：

```bash
rg -n "github_pat_|/root/|/workspace/|PHONE_NUMBER|PRIVATE_PHONE" \
  paper reports docs STATUS.md || true
```

注意：

提交包中不能出现 PAT、手机号、用户 home path、临时绝对路径。Report 内路径应相对 artifact root 或已 redacted。

## 12. 写入论文的建议措辞

Server case 建议：

```text
To reduce the gap between microbenchmarks and deployed systems, we add a loopback HTTP-server case study. The experiment protects the request-classification routine and drives the server through real TCP requests. Across 20 baseline/protected runs with 400 requests per run and concurrency 16, both variants produce identical response oracles. The measured protected/baseline median throughput and median latency ratios are close to 1.01x. This case remains a controlled server-scale sanity check rather than a production service deployment.
```

主流保护器 baseline 未完成时建议：

```text
We attempted to scope a same-workload mainstream-protector baseline. Because the Tigress binary requires a separate identity/license download flow, we do not include a fabricated or unlicensed Tigress result. We therefore retain mainstream-protector comparison as a validity threat and provide an executable protocol for future replication.
```

Windows evidence 建议：

```text
The Windows CI evidence now embeds GitHub Actions provenance and validates C/C++ native execution, Debug API launch survival, and detector positive-control audit events. It does not claim successful live Frida attach to a protected PE; that check remains skipped and is treated as future external validation work.
```

## 13. 90% 前必须补齐的证据

当前 5-agent 复审结论为：满足不低于 80%，真实区间约 84%-88%，不能诚实声明 90%。

要冲击 90%，建议按优先级补：

1. Protected PE live Frida attach 或外部硬件断点真实触发证据。
2. Tigress 或合法开源 protector 的同负载 baseline。
3. 更大 server/application case，例如多进程小型 HTTP 服务、SQLite CLI/server wrapper、Redis-like KV harness。
4. Android 真机 Frida/emulation 检测 report。
5. Owner 执行的人类初步用户研究。

每补一项后必须重新跑 5-agent x 3-round 审核；任一轮低于 80 或结论不同，就不能声明验收通过。
