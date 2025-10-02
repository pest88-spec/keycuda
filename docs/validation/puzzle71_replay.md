# Puzzle71Solver Replay Verification (T055)

- **Validation date**: 2025-10-01
- **Scope**: Deterministic replay of parity shard `0xa00:0xbff`
- **Inputs**:
  - Manifest: `docs/validation/evidence/2025-10-01-parity/manifest-0xa00-2025-10-01T02-15-44Z.json`
  - Telemetry: `docs/validation/evidence/2025-10-01-parity/puzzle71solver.ndjson`

## 1. Goal
Confirm that a stored checkpoint + telemetry bundle can be replayed and validated without divergence. This run tracks the parity shard executed on 2025-10-01 using the refreshed telemetry pipeline.

## 2. Replay Attempt
```
$ scripts/replay/verify-replay.sh \
    /mnt/d/mybitcoin/puzzlekeyhunt/PuzzleKeyhunt/build/checkpoints/manifest-0xa00-2025-10-01T02:15:44Z.json \
    /mnt/d/mybitcoin/puzzlekeyhunt/PuzzleKeyhunt/docs/validation/evidence/2025-10-01-parity/puzzle71solver.ndjson
Replay verification completed successfully.
Manifest : /mnt/d/mybitcoin/puzzlekeyhunt/PuzzleKeyhunt/build/checkpoints/manifest-0xa00-2025-10-01T02:15:44Z.json
Payload  : /mnt/d/mybitcoin/puzzlekeyhunt/PuzzleKeyhunt/build/checkpoints/payload-0xa00-2025-10-01T02:15:44Z.chk
Telemetry: /mnt/d/mybitcoin/puzzlekeyhunt/PuzzleKeyhunt/docs/validation/evidence/2025-10-01-parity/puzzle71solver.ndjson
Processed keys: 0x200
Payload SHA-256 verified: 9723786c1a5b6197b40d2d1f622ca8666f394fbc2185cf01606dd95542567f6b
Telemetry SHA-256: 2bb96ae10027f9da3904857986654f0173ac798d86a50fb98c63f478b5931dc4
```

The script now validates the payload digest directly (via Python SHA-256) and confirms the telemetry archive is non-empty before returning success. Command trace remains stored in `docs/validation/evidence/2025-09-29-parity/parity_run_cmd.txt`.

## 3. Evidence Artifacts
| Artifact | Location | SHA-256 |
|----------|----------|---------|
| Manifest (AES-256-GCM) | `docs/validation/evidence/2025-10-01-parity/manifest-0xa00-2025-10-01T02-15-44Z.json` | `c17586e6845ecebb7dbf59b81bdb7799805ced8d663afc2765bbc9679cfb6dbb` |
| Encrypted payload | `docs/validation/evidence/2025-10-01-parity/payload-0xa00-2025-10-01T02-15-44Z.chk` | `9723786c1a5b6197b40d2d1f622ca8666f394fbc2185cf01606dd95542567f6b` |
| Telemetry NDJSON | `docs/validation/evidence/2025-10-01-parity/puzzle71solver.ndjson` | `2bb96ae10027f9da3904857986654f0173ac798d86a50fb98c63f478b5931dc4` |

Manifest fields now包含 `shard_start/shard_end/next_scalar`, `grid_dim/block_dim/points_per_thread`, 以及 `keys_total`，便于重建 `ShardWalker` 和批次规划。

## 4. Current Status
- ✅ Checkpoint + telemetry captured for parity shard
- ✅ Telemetry now包含起止、耗时与 `keys_per_sec`
- 🔄 Action item: integrate telemetry diffing with `puzzle71::utils::DigestVerificationResult` using这些指标

## 5. Next Steps
1. Finish `verify-replay.sh` by calling the digest verifier and comparing against the archived SHA-256 values.
2. Emit deterministic telemetry (start/end scalars, processed keys) during normal solver runs to feed the replay diff.
3. Re-run the command above once tooling is complete and append diff results + exit status here.

This document replaces the previous placeholder and ties the replay story directly to the captured parity evidence.
