# Checkpoint & Replay Runbook (Puzzle71Solver)

## Enabling Checkpoints
1. Provide passphrase file (future implementation) and run with `--enable-checkpoint`.
2. Solver generates manifests under `checkpoints/manifest-*.json`（当前 stub 会为空），稍后通过 AES-GCM + `payload_sha256` 字段存储真实摘要。
3. Checklist: ensure manifest timestamp <30 days, rotate passphrase after sustained runs.

## Resuming / Replaying
1. Launch with `--replay-manifest <path>` (see quickstart). Solver loads manifest、调用 `VerifyManifestDigest`。
2. Replay verifier script:
```bash
scripts/replay/verify-replay.sh checkpoints/manifest-0001.json telemetry/replay.jsonl
```
   - Stub currently prints提示；后续会调用 digest 工具比较 SHA-256。

## Maintenance
- `scripts/purge-checkpoints.sh <retention>` 删除过期清单（当前为占位）。
- `scripts/digest/check-artifact-digests.sh` 生成 `digests/latest.json`，记录各 artifact 的 SHA-256。
- 在 QA/发布流程中运行 `scripts/run-qa.sh`（待实现）以执行 replay + digest 验证。

## TODO
- 实现 AES-256-GCM 加密/解密以及 manifest 中的 nonce/salt/hash 字段。
- 在 purge & digest 脚本中调用实际验证逻辑，输出详细报告。
- 将验证结果纳入最终报告和合规检查表。
