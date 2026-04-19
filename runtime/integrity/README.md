# runtime/integrity

- 对应 plan：§16.1 / §16.3
- 当前状态：region-registry-ready + crc32-dual-tier + periodic-sweeper

## 组件
- `ProtectedRegion`：受保护区域描述（`name/base/size/expected_sha256/expected_crc32/flags`）
- `RegionRegistry`：线程安全注册表，支持 `register_region` / `unregister` / `all` / `verify_one` / `verify_one_fast` / `verify_all`
- `crc32_compute(...)` / `Crc32Stream`：标准 CRC-32 IEEE 802.3（多项式 `0xEDB88320`，反射输入/输出，init/xor-out 均为 `0xFFFFFFFF`）
- `PeriodicSweeper`：可选周期巡检线程，按环境变量启动

## CRC32 + SHA-256 双层语义
- 注册阶段：
  - 若 `expected_crc32` 为空，则捕获当前内存的 CRC32
  - 若 `expected_sha256` 为全 0，则捕获当前内存的 SHA-256
- 校验模式：
  - `Mode::fast`：只校验 CRC32，用于低成本巡检
  - `Mode::authoritative`：先算 CRC32，再总是计算 SHA-256 并以 SHA-256 作为最终判定（为保持 subtask 16 的兼容语义，默认 `verify_one(...)` 仍走完整 SHA-256）
- 事件上报：
  - `fast` 模式失配：记录 `integrity_fast_mismatch`
  - `authoritative` 模式确认失配：记录 `integrity_authoritative_mismatch`，随后通过 `RuntimeState::observe(integrity_failed, region)` 进入统一状态机

## Periodic sweeper
- 默认关闭。
- 当 `VMP_INTEGRITY_SWEEP_MS` 存在且 `>= 50` 时，`RuntimeState::init_once()` 会启动全局 sweeper。
- 巡检线程行为：
  1. 每个周期执行 `RegionRegistry::verify_all(Mode::fast)`
  2. 任一 region 触发 CRC32 失配时，立即执行 `verify_one(name, Mode::authoritative)`
  3. 仅当 authoritative 校验也失败时，才上报 `integrity_failed`
- `RuntimeState::shutdown()` 会停止 sweeper 并卸载完整性观察器，避免测试之间串扰。

## Loader / 模块容器 CRC32
- VM1 容器版本：`kVm1Version = 3`
- VM2 容器版本：`kVm2Version = 3`
- 旧版本模块（不带 CRC32 字段）会被严格拒绝，不做兼容降级。
- VM1 新头部：
  - `magic[4]`
  - `version:u16`
  - `module_flags:u16`
  - `entry_pc:u32`
  - `code_size:u32`
  - `const_count:u32`
  - `crc32:u32`
  - 后续 body：`code + const_pool_entries`
- VM2 新头部：
  - `magic[4]`
  - `version:u16`
  - `module_flags:u16`
  - `entry_pc:u32`
  - `code_size:u32`
  - `const_pool_size:u32`
  - `crc32:u32`
  - 后续 body：`code + const_pool`，其后仍跟 `key_context_id[16]`
- CRC32 覆盖范围：
  - VM1：`code + const_pool_entries`
  - VM2：`code + const_pool`
- 载入校验失败时：
  - VM1：抛异常并记录 `vm1_module_crc_mismatch`
  - VM2：抛异常并记录 `vm2_module_crc_mismatch`

## CLI / 调试工具
- `vmp-vm1-asm --crc-only <module.vm1>`：打印模块 body CRC32（不重汇编）
- `vmp-integrity-probe <file>`：打印整个文件的 CRC32 + SHA-256
- `vmp-integrity-probe --tamper <offset>:<byte> <file>`：在 `/tmp` 复制后篡改并重算，若发现不一致则非零退出

## 审计与状态机
- 运行时完整性确认失败会写 `integrity_authoritative_mismatch` + `integrity_failed` + `state_transition`
- 快速巡检误报/测试注入仅写 `integrity_fast_mismatch`，不降级、不终止
