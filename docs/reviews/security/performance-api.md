# 安全评审：Performance Control API

**评审日期**：2025-10-08  
**评审范围**：`/benchmarks` 与 `/tuning-sessions` HTTP 接口（参见 `src/api/performance_controller.cpp`、`src/api/tuning_controller.cpp`），以及相关脚本 `ci/performance_gate.sh`、`scripts/alerts/performance_alert.py`。

## 1. 接口与数据流

| 组件 | 描述 | 主要输入/输出 |
|------|------|---------------|
| `/benchmarks` (POST) | 基准结果入库，写入 `telemetry/baselines/<GPU>/` | JSON payload（PerformanceBaseline + metadata），输出 `baseline_id`、存储路径 |
| `/tuning-sessions` (POST/POST complete) | 调优会话评估，调用 `SessionController` 并可在完成阶段写 baseline | Start: candidate/metrics 列表；Complete: rollback 结果、可选 baseline |
| `PerfStore` | 统一基线存储，热降级自动隔离 | 入参：baseline 对象 + metadata；输出：归档路径 |
| `Performance Gate` 脚本 | CI 中比较 benchmark 与基线 | 输入：最新 summary + baselines；输出：`logs/ci/performance_gate.json` |
| 告警脚本 `scripts/alerts/performance_alert.py` | 解析 guardrail breach，发送 Slack/Email | 输入：alert JSON + baseline；输出：通知及 `logs/alerts/performance_alert.json` |

### 数据存储
- **主基线目录**：`telemetry/baselines/<GPU>/`（结构化 JSON，含 driver/cuda/firmware）。
- **隔离目录**：`telemetry/baselines/quarantine/<GPU>/`，热降级或 metadata 标记降级时写入。
- **遥测结果**：`benchmarks/*.json` 和 `telemetry/runs/*.jsonl`。

## 2. 访问与鉴权
- API 建议部署在内网 CI 环境，仅接受 `https://solver.example.com/api` 内部调用；需由上游网关（如 Envoy/Nginx）加上 mTLS/Token 验证。
- 当前实现未内置鉴权，部署时需在 API Gateway 层启用。
- 入参使用 JSON，全部通过 nlohmann::json 的显式结构体转换，避免 SQL/命令注入。

## 3. 保密性与完整性
- 所有基线输出包含硬件/驱动信息，不含敏感密钥；仍需限制文件目录权限（仅运维组可写）。
- `PerfStore` 写文件使用 `std::ofstream` + truncate，不覆盖旧数据；使用 `quarantine` 隔离故障样本。
- `ci/performance_gate.sh` 与告警脚本均在 `logs/` 目录输出结构化结果，供后续审计。

## 4. 威胁模型 & 缓解
| 威胁 | 场景 | 缓解措施 |
|------|------|-----------|
| 未授权写入基线 | 攻击者调用 `/benchmarks` 写入伪造文件 | 限制 API 网关；CI pipeline 仅从受控账号触发；文件系统权限限制为运维用户 |
| JSON 注入导致解析异常 | 入参字段类型不匹配或超大字符串 | 使用显式 `get<T>`，异常直接抛出；`PerfStore` 对无效 JSON 直接忽略 |
| 热降级样本污染基线 | GPU 高温或 NVML 报告 degradation | `PerfStore` 自动写入 quarantine；`ci/performance_gate.sh` 比对 `slowdown_injected`/`thermal` 字段 |
| 告警发送凭据泄露 | Slack/SMTP 凭据保存在脚本 | 建议通过环境变量或 Secrets Manager 注入，脚本未 hardcode；dry-run 默认关闭实际发送 |
| 性能阈值被篡改 | 攻击者修改 `ops/thresholds.yaml` | 文件纳入代码评审，`PUZZLE71_THRESHOLD_FILE` 需在 CI 机密存储（只读），变更需记录 |

## 5. 审计与日志
- 必须保留以下日志：
  - `logs/ci/performance_gate.json`：每次 CI 对比结果。
  - `logs/alerts/performance_alert.json`：告警摘要与发送结果。
  - API 层应启用访问日志（时间、调用方、状态码）。
- 建议 30 天滚动归档，可交给集中日志平台（Elastic/Stackdriver）。

## 6. 安全测试
- Stage 1 的 `ci/security_scan.sh`（flawfinder + constant-time 审计）必须在每次构建执行，并确保无 High 严重漏洞。
- TuningPipeline 集成测试 (`./build/puzzle71_tests --gtest_filter=TuningGuardrailsTest.RollbackTriggersQuarantineBaseline`) 需在 CI 中运行；失败视为安全回滚事件。

## 7. Outstanding Items
- [ ] 部署阶段需添加 API 鉴权（JWT 或 mTLS）。
- [ ] 定期轮换 Slack Webhook 与 SMTP 凭据。
- [ ] 将 `scripts/alerts/performance_alert.py` dry-run 输出纳入 SIEM 监控以确认告警链路。

评审完成，当前实现符合铁笼协议 v3.0 的安全要求（鉴权执行需在部署层面落实）。
