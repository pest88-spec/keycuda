# Puzzle71Solver Compliance Checklist (Draft)

## Scope Guard & Telemetry
- [ ] CLI requires `--keyspace`, `--target-address`, `--operator-id`, `--operator-purpose`.
- [ ] Telemetry JSONL captures device ID、operator metadata、alerts。
- [ ] Prometheus textfile includes throughput + checkpoint metrics。（脚本待实现）

## Checkpoint Handling
- [ ] `--enable-checkpoint` 测试通过：manifest 写入 `checkpoints/`。
- [ ] `scripts/purge-checkpoints.sh` 已执行（stub，待替换为真实执行）。
- [ ] 最新 `digests/latest.json` 已生成（stub版本记录 SHA-256 列表）。

## Replay & Verification
- [ ] `scripts/replay/verify-replay.sh` 对关键 manifest 运行（目前为占位）。
- [ ] 重放检测结果归档在 `docs/validation/`（T055 未完成）。

## Documentation
- [X] `quickstart.md` 更新 checkpoint/replay 流程。
- [X] `docs/performance.md` 列出 baseline 与 TODO。
- [X] `docs/runbooks/checkpoint.md` 添加操作指引。

## TODO (待完成)
- 实现 AES-GCM 与 SHA-256 校验逻辑。
- 更新脚本运行结果（QA、benchmark、digest）并保存输出证据。
- 填写最终合规结论与负责人签字。
