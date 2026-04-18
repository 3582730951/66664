# runtime/state

- 对应 plan：§16
- 范围：运行时状态机最小闭环，供 loader、审计、后续完整性/JIT 子系统共享
- 当前状态：minimal-init-ready

## 本轮交付
- `RuntimeState` 单例：`init_once(audit*, RuntimeConfig)` / `shutdown()`
- 标志位管理：`set_flag()` / `check_flag()`
- 事件映射：`observe(RuntimeEventKind)` → `integrity_failed` / `env_anomaly` / `key_rotated`
- 审计句柄透传：`get_audit()`

## 约束
- 本轮仅提供 loader 所需的初始化与标志位接口。
- 热点画像、JIT 元数据、复杂策略状态将在后续子任务扩展。
