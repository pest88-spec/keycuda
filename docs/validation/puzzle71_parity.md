# Puzzle71Solver GPU ⇄ CPU Parity Validation

- **Validation date**: 2025-10-01
- **Operator**: doc (purpose: parity)
- **GPU**: NVIDIA GeForce RTX 2080 Ti (SM 75) — CUDA 12.4 stack (WSL2)
- **Solver build**: `Puzzle71Solver` (CMake target, commit HEAD of 001-implement-puzzle71solver-mred)

## 1. Objective
Demonstrate that the fused CUDA kernel and the bitcoin-core/secp256k1 CPU verifier produce identical HASH160 results for a known scalar while the solver routes matching candidates to the audit trail (`luck.txt`). This satisfies T054’s evidence requirement prior to the full keyspace sweep.

## 2. Test Scenario
| Item | Value |
|------|-------|
| CLI command | `cd build && ./Puzzle71Solver --keyspace 0xa00:0xbff --target-address 1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU --operator-id parity --operator-purpose doc --device 0 --parity-test-scalar 0xabc --telemetry-jsonl ../docs/validation/evidence/2025-10-01-parity --luck-file ../docs/validation/evidence/2025-10-01-parity/luck_parity_run.txt --enable-checkpoint` |
| Working directory | `build/` |
| Target HASH160 (parity override) | `cf8ab07a b8fdf651 83659249 2b9b036b 7ca0c906` |
| GPU launch | grid 2 × block 256, points/thread 1 (512 keys/step) |
| Key range scanned | `0xa00 – 0xbff` (test shard) |

The override uploads the HASH160 derived from scalar `0xabc` into constant memory. Solver now compares candidates against this hash (not the canonical puzzle constant) so the parity sample is preserved.

## 3. Results
| Artifact | Evidence |
|----------|----------|
| GPU candidates | 1 candidate returned in a single step (logged in solver stdout) |
| CPU parity | `crypto::DerivePublicKey` confirmed compressed public key and address match; no mismatches observed |
| Audit trail | `docs/validation/evidence/2025-10-01-parity/luck_parity_run.txt` records `0xabc 1KvP21twgz7RTnDbAztPGTjx3YCAk3Evpu` |
| Checkpoint | `docs/validation/evidence/2025-10-01-parity/manifest-0xa00-2025-10-01T02-15-44Z.json` (AES-256-GCM payload + SHA-256 digest) |
| Telemetry | `docs/validation/evidence/2025-10-01-parity/puzzle71solver.ndjson` (structured NDJSON emitted by solver) |
| Step throughput | 512,000 keys/sec over 512-key shard (derived from telemetry sample) |
| Checkpoint metadata | Manifest includes `block_dim`, `grid_dim`, `points_per_thread` enabling deterministic resume |

### GPU instrumentation snapshot
```
Block Size     : 256 threads (32-aligned)
Grid Size      : 2
Points/thread  : 1
Keys/step      : 512
```

### Telemetry sample
```
{"candidate_count":1,"device_id":0,"elapsed_ms":1,"keys_per_sec":512000.0,"operator_id":"parity","operator_purpose":"doc","processed_keys":512,"processed_keys_hex":"0x200","shard":{"end":"0xbff","next_scalar":"0xc00","start":"0xa00"},"status":"ok","timestamp":"2025-10-01T02:15:44Z"}
```

### Hash & Digest checksums
```
48e129f3bc2ca6b0fd5204fc7aa82df4ccedd34f7428faebd5b32f210d1ab9db  luck_parity_run.txt
2bb96ae10027f9da3904857986654f0173ac798d86a50fb98c63f478b5931dc4  puzzle71solver.ndjson
c17586e6845ecebb7dbf59b81bdb7799805ced8d663afc2765bbc9679cfb6dbb  manifest-0xa00-2025-10-01T02-15-44Z.json
9723786c1a5b6197b40d2d1f622ca8666f394fbc2185cf01606dd95542567f6b  payload-0xa00-2025-10-01T02-15-44Z.chk
```
SHA-256 values generated via `sha256sum` (OpenSSL backend) on 2025-10-01.

## 4. Replay Hook (T055 linkage)
`scripts/replay/verify-replay.sh build/checkpoints/manifest-0xa00-2025-10-01T02:15:44Z.json docs/validation/evidence/2025-10-01-parity/puzzle71solver.ndjson`
→ verifies SHA-256 for encrypted payload and telemetry archive (see replay报告 for full log)。

## 5. Status & Next Steps
- ✅ GPU/CPU parity sample captured and archived (T054 evidence)
- ✅ Telemetry payload upgraded to structured NDJSON
- 🔄 Expand telemetry semantics (per-device throughput, deterministic seeds) before scaling to additional shards

This report supersedes earlier placeholder content and is ready for inclusion in the compliance evidence pack.
