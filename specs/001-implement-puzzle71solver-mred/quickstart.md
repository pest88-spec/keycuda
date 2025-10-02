# Quickstart: Puzzle71Solver CUDA Implementation

## Prerequisites
- Ubuntu 22.04 with CUDA Toolkit 11.8 and NVIDIA driver ≥ 535.
- NVIDIA GPUs ≥ RTX 2080 Ti (verify via `nvidia-smi`), optional multi-GPU node.
- CMake ≥ 3.18, GCC ≥ 10, GoogleTest (FetchContent).
- OpenSSL CLI for checkpoint passphrase derivation.
- `config/puzzle71.yaml` populated via Phase 3.1 scaffolding (keyspace, operator defaults, replay seeds).

## 1. Build & Dry Run
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc)"
./Puzzle71Solver \
  --keyspace 0x400000000000000000:0x4000000000000000FF \
  --target-address 1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU \
  --operator-id test-op \
  --operator-purpose validation \
  --device 0 \
  --dry-run
```
- 期望输出：exit 0，生成 Telemetry 目录并打印配置校验日志；无 GPU 运算。
- 建议额外运行一次错误地址以验证 scope guard（应 exit 20 并输出警告）。

## 2. Launch Multi-GPU Scan
```bash
./Puzzle71Solver \
  --keyspace 0x400000000000000000:0x7fffffffffffffffff \
  --target-address 1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU \
  --operator-id prod-op \
  --operator-purpose main-scan \
  --device 0,1 \
  --prometheus-export metrics \
  --telemetry-jsonl telemetry \
  --luck-file luck.txt
```
- `luck.txt` 自动追加 `<hex_scalar> <Base58_address>`。
- 可选：`--enable-checkpoint` 在默认目录 `checkpoints/` 生成加密清单；建议配合 `--checkpoint-passphrase`（待 crypto 实现）。
- 监控 `telemetry/*.jsonl` 与 `metrics/puzzle71.prom`；当吞吐 <1,000M keys/sec 或 checkpoint latency >2,000 ms 会提醒。

## 3. Checkpoint Resume & Replay
```bash
./Puzzle71Solver \
  --keyspace 0x400000000000000000:0x4000000000000FFFFF \
  --target-address 1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU \
  --operator-id replay-op \
  --operator-purpose replay \
  --device 0 \
  --replay-manifest checkpoints/manifest-example.json \
  --telemetry-jsonl telemetry/replay \
  --prometheus-export metrics
```
- Solver 会加载 manifest、执行摘要比对并重新播放分片（`payload_sha256` 字段将用于后续 SHA-256 校验）。
- 可使用 `scripts/replay/verify-replay.sh <manifest> <telemetry-jsonl>` 进行额外校验（后续版本将输出详细 diff）。

## 4. Performance Benchmark Verification
-**WSL2 示例**
```bash
./scripts/run-benchmarks.sh --devices 0 --warmup 1 --samples 5
```
- 脚本会执行预热与测量，输出 `benchmarks/latest.json` 并自动对比基线（WSL2 默认 50M keys/sec；原生 Linux 仍需 ≥1,000M keys/sec）。
- Nsight 轮廓：`./tools/nsight/puzzle71_profile.sh --device 0`（脚本后续提供详参）。

## 5. Cleanup & Reporting
```bash
./scripts/purge-checkpoints.sh --dry-run --keep-days 30  # 删除 >30 天 checkpoint 前先审查
./scripts/digest/check-artifact-digests.sh  # 生成 digests/latest.json
./scripts/run-qa.sh                     # QA 验证流水线（tests + replay + perf smoke）
./scripts/generate-report.sh            # 汇总 reports/puzzle71-run-<date>.md
```
- 检查 `luck.txt` 与 `digests/latest.json`，确保记录与摘要完整。
- 文档/治理：更新 `docs/governance/compliance-checklist.md`、保存 replay 证据于 `docs/validation/`。
- Crypto 与 digest 脚本当前为占位，后续任务会替换为真实实现。
