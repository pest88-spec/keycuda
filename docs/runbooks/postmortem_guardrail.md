# Guardrail Breach Post-Mortem Template

**Incident ID**: `<YYYYMMDD-hhmm>-<short description>`  
**Reported By**: `<name / automation>`  
**Date Range**: `<start timestamp> – <end timestamp>`  
**GPU / Environment**: `<GPU model, cluster, driver version>`

## 1. Summary
- **Impact**: `<services affected, throughput deviation>`
- **Trigger**: `<ThroughputDrop / ParityError / ThermalDegradation>`
- **Detection Source**: `<ci/performance_gate.sh | alerts/performance_alert.py | manual>`
- **Rollback Applied**: `<yes/no>`

## 2. Timeline
| 时间 (UTC) | 事件 |
|------------|------|
| hh:mm | Guardrail breach detected (`logs/ci/performance_gate.json` / alert JSON) |
| hh:mm | Notifications sent (Slack/Email) |
| hh:mm | Mitigation applied (rollback / re-run / HW swap) |
| hh:mm | System restored |

## 3. Observability Evidence
- `benchmarks/history/<run>.json`（上传到 artifact storage）
- `telemetry/runs/<run_id>.jsonl`（关键批次）
- `logs/ci/performance_gate.json`（verdict、slowdown_injected、parity）
- `logs/alerts/performance_alert.json`（baseline provenance）
- 相关 GPU 指标（NVML 温度、功率）

## 4. Root Cause Analysis
- **Primary Factors**: `<config error / hardware degradation / code regression>`
- **Contributing Factors**: `<missing alerts / thresholds>`
- **Why chain**: `<5 Whys or equivalent>`

## 5. Remediation Actions
- **Immediate Fix**: `<rollback / rerun benchmark / replace GPU>`
- **Follow-up Tasks**:
  1. `<update ops/thresholds.yaml …>`
  2. `<add regression test …>`
  3. `<coordinate with hardware team …>`

## 6. Preventive Measures
- **Automation**: `<extend ci/performance_gate.sh coverage / adjust alerts>`
- **Documentation**: `<update quickstart/observability>`
- **Training**: `<knowledge sharing>`

## 7. Sign-off
- **Prepared By**: `<name & date>`
- **Reviewed By**: `<security reviewer>`
- **Next Review**: `<date>`

---
> 填写完成后，将文档提交到 `docs/runbooks/history/`，并在运维频道同步链接。
