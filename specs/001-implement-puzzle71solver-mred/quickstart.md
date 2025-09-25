# Quickstart: Puzzle71Solver CUDA Implementation

## Prerequisites
- Ubuntu 22.04 with CUDA Toolkit 11.8 and NVIDIA driver ≥ 535.
- NVIDIA GPUs ≥ RTX 2080 Ti (verify via `nvidia-smi`), optional multi-GPU node.
- CMake ≥ 3.18, GCC ≥ 10, GoogleTest available via FetchContent.
- OpenSSL CLI for passphrase derivation.
- Project constants file `config/project.yml` populated with Puzzle #71 values.

## 1. Build & Dry Run
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./Puzzle71Solver --keyspace 0x400000000000000000:0x40000000000FFFFF \
                 --target-address 1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU \
                 --dry-run
```
- Expected: exit 0, telemetry directory and config validation logs, no GPU work performed.
- Run once with an incorrect `--target-address` to verify scope guard failure (expected non-zero exit and explicit error); correct the address before proceeding.
- Verify that `luck.txt` remains untouched; it will be created only when a match is found during real runs.

## 2. Launch Multi-GPU Scan
```bash
./Puzzle71Solver --keyspace 0x400000000000000000:0x7fffffffffffffffff \
                 --target-address 1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU \
                 --device 0,1,2,3 \
                 --prometheus-export ../metrics
```
- `luck.txt` is appended automatically when a match is found (hex scalar + Base58 address).
- Optional extra flag `--enable-checkpoint` enables periodic encrypted checkpointing using default locations; omit it for baseline runs.
- Monitor `logs/puzzle71solver-*.jsonl` for telemetry packets.
- Alert triggers when throughput <1,000M keys/sec or checkpoint latency >2,000 ms.

## 3. Checkpoint Resume & Replay
```bash
./Puzzle71Solver --enable-checkpoint \
                 --replay-checkpoint ../checkpoints/shard-0003.manifest \
                 --target-address 1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU \
                 --device 0
```
- Replays shard with CPU verification enabled; omit these flags entirely if checkpointing is not needed.
- Validate manifest integrity via `hash160_digest` and ensure timestamps <30 days old.

## 4. Performance Benchmark Verification
```bash
./scripts/run-benchmarks.sh --devices 0,1 --samples 5
```
- Script aggregates median keys/sec and compares to baseline table (2080 Ti ≥1,000M, 3090 ≥2,000M, A100 ≥4,000M).
- Store results in `benchmarks/latest.json` with hardware metadata.

## 5. Cleanup & Reporting
- Review `luck.txt` (newline-delimited matches) and archive securely.
- If checkpoints were enabled, remove expired checkpoints with `./scripts/purge-checkpoints.sh --older-than 30d`.
- Export final report: `./scripts/generate-report.sh --output reports/puzzle71-run-<date>.md`.
- Rotate optional checkpoint passphrase materials and document in compliance log.
