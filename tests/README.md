# tests

- 对应 plan：验收支撑与 §17 final matrix
- 范围：CTest、跨语言 frontend parity、native/VM 功能一致性、JIT 热点、审计事件、平台门控、性能采样

## Final matrix

| Plan | CTest entry | Artifact / note |
| --- | --- | --- |
| 17.1 标注测试 | `final_matrix_17_1_annotation_parity` | `annotation_cpp.policy.json` / `annotation_rust.normalized.json` |
| 17.2 功能一致性测试 | `final_matrix_17_2_functional_consistency` | `functional_consistency.json` |
| 17.3 JIT 与热点测试 | `final_matrix_17_3_jit_hotspot` | `jit_hotspot.json` |
| 17.4 审计事件测试 | `final_matrix_17_4_audit_events` | `audit_events.json` |
| 17.5 多平台与多架构测试 | `final_matrix_17_5_platform_matrix` | 显式 `SKIP_REASON`；依赖专用 runner/工具链 |
| 17.6 性能测试 | `final_matrix_17_6_perf` | `perf_report.json`（仅要求脚本跑完） |
| 汇总 | `final_matrix_summary_test` + `final_matrix_summary` target | `print_summary.py` 表格输出 |

## Notes

- 17.5 在单一 Linux/x64 开发容器内不伪造跨平台结果；若未处于对应 runner，测试以显式 skip reason 标记。
- 17.6 不以性能数字为通过门槛，仅要求报告成功生成。
