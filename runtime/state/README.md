# runtime/state

- 对应 plan：§7.5、§8.1-§8.4、§9、§16
- 本轮状态：profile-loader-online-fusion-scheduler-ready

## 提供能力

### 1. 离线画像格式
- 头文件：`runtime/state/include/vmp/runtime/state/profile.h`
- 结构：
  - `ProfileEntry { module_id, pc, hits, hot_class, importance }`
  - `OfflineProfile { version, source_seed, entries, meta }`
- 序列化：JSON，顶层 schema 固定为 `vp1`
- 约束：
  - `importance` 必须在 `[0, 1]`
  - `hot_class` 必须在 `[0, 3]`
  - `(module_id, pc)` 不允许重复
  - 未知字段严格拒绝

### 2. 在线记录器
- 头文件：`runtime/state/include/vmp/runtime/state/hot_recorder.h`
- `HotRecorder` 为线程安全累加器，记录：
  - 函数进入
  - 基本块进入
  - trace 边
  - JIT hit / miss
  - 域切换
  - 敏感数据瞬时明文访问成本
- `snapshot()` 返回不可变值拷贝，供调度器消费。

### 3. 启动加载与画像融合
- `RuntimeState::init_once(...)`：
  - 若设置 `VMP_OFFLINE_PROFILE`，启动时自动加载离线画像
  - 成功写入 `profile_loaded` 审计事件
  - 失败写入 `profile_load_failed` 审计事件
- 融合规则：
  - 默认在线权重为 `0.4`
  - 运行时间不足 `60s` 时在线权重强制为 `0`
  - 可通过 `VMP_PROFILE_ONLINE_WEIGHT` 覆盖默认值
- 在线修正**只能**影响：
  - JIT 排序
  - cache 大小分配
  - trace stitching / break
  - warmup 时机
- 在线修正**不能**改变：
  - `Policy IR` 中的 `protection_domain`
  - `sensitivity_level`
  - `plaintext_budget`
  - `integrity_level`
  - `reaction_policy`

### 4. 热点调度器
- 头文件：`runtime/state/include/vmp/runtime/state/scheduler.h`
- `HotScheduler` 输出 `ScheduleAction`：
  - `jit_compile_now`
  - `jit_evict`
  - `trace_stitch`
  - `trace_break`
  - `cache_resize`
  - `warmup_kick`
- 每模块预算默认约束：`[1 MiB, 16 MiB]`
- 调度器可通过既有 `Vm1Jit` / `Vm2Jit` hook 应用决策，不引入新的大接口面。

### 5. 审计事件
- `profile_loaded`
- `profile_load_failed`
- `scheduler_decision`
- `scheduler_skipped`
- `trace_stitch_applied`
- `cache_resize`

## CLI

### `vmp-protect`
- `--profile-out <path>`：输出基线画像 JSON（schema `vp1`）

### `vmp-vm1-run` / `vmp-vm2-run`
- `--profile <path>`：运行前加载离线画像
- `--profile-out <path>`：运行后导出在线状态快照为画像 JSON

### `vmp-profile-tool`
- `merge <a.json> <b.json> --output <merged.json>`
- `diff <a.json> <b.json>`
- `validate <p.json>`

## 环境变量
- `VMP_OFFLINE_PROFILE`：启动时自动加载的离线画像路径
- `VMP_PROFILE_ONLINE_WEIGHT`：在线修正权重，默认 `0.4`

## 测试覆盖
- round-trip / validator / 并发 recorder
- 融合不改 Policy IR 硬约束
- 调度器 compile / evict / resize
- 审计记录
- `vmp-profile-tool` merge / diff / validate 端到端
