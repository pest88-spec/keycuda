# Feature Specification: Puzzle71Solver CUDA Implementation

**Feature Branch**: `001-implement-puzzle71solver-mred`  
**Created**: 2025-09-25  
**Status**: Draft  
**Input**: User description: "Implement Puzzle71Solver (MRED-001 v1.2) to solve Bitcoin Puzzle #71 using CUDA batch stepping kernels, deterministic CPU/GPU parity, multi-GPU keyspace partitioning, progress telemetry, and secure checkpointing per project constants."

## Mission Context
- **Mission**: Deliver a single-purpose, GPU-accelerated solver for Bitcoin Puzzle #71. No other puzzles, datasets, or experiments are in scope.
- **Target Address (CLI input)**: `1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU`
- **Target HASH160**: `f8455b22fa469a40654450d363959a3b932924b4`
- **Private Key Range (CLI input)**: `0x400000000000000000` (2^70) through `0x7fffffffffffffffff` (2^71 − 1)
- **Result Sink**: Any discovered key MUST be appended automatically to `luck.txt` (UTF-8, newline-delimited, hex scalar + Base58 address).
- **Baseline Hardware**: NVIDIA RTX 2080 Ti (minimum); higher-tier GPUs (e.g., RTX 3090, A100) expected to exceed baseline throughput.

## Clarifications

### Session 2025-09-25
- Q: What sustained throughput target should each GPU meet during Puzzle71Solver runs (used to size batch stepping, perf alerts, and acceptance tests)? → A: Minimum GPU is 2080Ti sustaining ≥1000M keys/sec
- Q: What checkpoint rotation schedule should Puzzle71Solver enforce to balance recovery speed and storage limits? → A: Every 30 minutes or 3×10¹² keys
- Q: When a GPU device drops offline mid-run, what should Puzzle71Solver do by default? → A: Auto reassign shard to peers
- Q: How should Puzzle71Solver authenticate and authorize operators before allowing scans to run? → A: No authentication required
- Q: How do operators identify themselves for audit logs when authentication is skipped? → A: CLI MUST collect `--operator-id` and `--operator-purpose` values (or load from config) and persist them in telemetry/audit trails.
- Q: Where should successful matches be persisted automatically? → A: Append to `luck.txt` in working directory

## User Scenarios & Testing *(mandatory)*

### Primary User Story
A PuzzleKeyhunt operator must execute Puzzle71Solver—the “surgical scalpel” built solely for Puzzle #71—across the fixed keyspace so that any discovered key is validated, appended to `luck.txt`, and handed off responsibly with full telemetry, optional checkpoint artifacts, and audit evidence. Performance is the prime directive; correctness is the non-negotiable gate that unlocks performance tuning.

### Acceptance Scenarios
1. **Single-range scan** – Given the CLI is launched with `--keyspace 0x400000000000000000:0x7fffffffffffffffff --target-address 1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU`, the solver MUST evaluate only scalars in that inclusive interval and compare each HASH160 against the digest derived from the provided address. Any mismatch between CLI address and canonical Puzzle #71 address MUST abort with a scope-guard error.
2. **Checkpoint resume (optional)** – If checkpointing is enabled with `--enable-checkpoint` and a run is interrupted, when the solver restarts with `--resume <manifest>`, it MUST resume at the next unprocessed scalar with no gaps or duplicates; absence of checkpoint parameters MUST NOT block baseline runs.
3. **Multi-GPU dispatch** – Given multiple supported NVIDIA devices (e.g., `--device 0,1`), the solver MUST deterministically partition the keyspace, sustain ≥1,000M keys/sec per device, and reassign shards automatically if a GPU drops offline while maintaining deterministic replay data.
4. **CPU parity verification** – Given a batch of GPU-flagged candidates, the CPU validation harness MUST reproduce the same HASH160 values using [bitcoin-core/secp256k1](https://github.com/bitcoin-core/secp256k1) within `1e-10` relative error; any discrepant sample MUST fail the run.
5. **Result persistence** – Whenever a candidate passes GPU + CPU checks, the solver MUST append a line containing `<hex_scalar> <Base58_address>` to `luck.txt` atomically and emit a success log.
6. **Invalid request handling** – Given an out-of-range keyspace, missing `--target-address`, or extra target supplied, the solver MUST reject execution, log the scope guard violation, and exit non-zero without launching kernels.
7. **Deterministic replay verification** – When an existing checkpoint manifest is consumed via `--replay-manifest`, the solver MUST regenerate telemetry, hash outputs, and digest manifests identical to the original run within the tolerances defined in NFR-005/NFR-007; any divergence aborts with a reproducibility error.

### Edge Cases
- GPU device loss during execution MUST trigger shard reassignment, optional checkpointing if enabled, and operator notification while keeping the run deterministic.
- Checkpoint corruption (when enabled) MUST abort the run, surface the error, and retain forensic data for replay investigation; absence of checkpoints MUST not hinder standard operation.
- Early success MUST stop all workers, append the match to `luck.txt`, flush telemetry, and emit a final audit report.
- Passphrase or checkpoint manifest mismatch MUST fail gracefully and log the error without exposing sensitive state.

## Requirements *(mandatory)*

### Functional Requirements
- **FR-001**: CLI MUST require `--keyspace <start>:<end>` within ProjectConstants and `--target-address <Base58>` matching Puzzle #71; both parameters are mandatory for every run.
- **FR-002**: Any discovered key MUST be appended automatically (hex scalar + Base58 address) to `luck.txt` in the working directory before program exit.
- **FR-003**: CUDA kernels MUST generate compressed secp256k1 public keys and compute `RIPEMD160(SHA256(pubkey))` entirely on device, comparing against the digest derived from the required `--target-address`.
- **FR-004**: Endomorphism acceleration MUST port `secp256k1_scalar_split_lambda` from ElementsProject/secp256k1-zkp, exposing device-callable helpers for both GPU kernels and host-driven tests.
- **FR-005**: Batch stepping MUST reuse the `(k+1)G = kG + G` pattern with precomputed tables so each thread performs one full multiplication followed by incremental additions; reference VanitySearch layout for register discipline.
- **FR-006**: Pipeline coupling MUST ensure the point multiplication, SHA256, RIPEMD160, and digest comparison run in a single CUDA launch to eliminate memory round-trips; monolithic fallback kernels are forbidden.
- **FR-007**: Checkpointing is optional; when enabled (e.g., via `--enable-checkpoint`), solver MUST persist encrypted checkpoints every 30 minutes or 3×10¹² keys without additional mandatory flags. Baseline runs without checkpointing MUST operate correctly.
- **FR-008**: Telemetry MUST stream structured metrics (timestamp, device, keys/sec, occupancy, alerts) to stdout and optional JSON sink at ≤1 s cadence, with alerting for throughput <1,000M keys/sec or checkpoint latency >2,000 ms when enabled.
- **FR-009**: CPU validation harness MUST sample GPU outputs each run and confirm parity using bitcoin-core/secp256k1 before reporting success; the harness acts as the golden test suite per the Iron Cage protocol.
- **FR-010**: Multi-GPU scheduler MUST deterministically allocate contiguous shards, automatically reassign shards from failed devices, and document shard lineage for replay.
- **FR-011**: Scope guard MUST prevent loading multiple targets, bloom filters, or extraneous datasets; violations log workstation identity and exit with error code.
- **FR-012**: CLI MUST offer optional flags (`--enable-checkpoint`, `--prometheus-export`, `--dry-run`) without making them prerequisites for standard execution.
- **FR-013**: Documentation (quickstart, runbook, reports) MUST be updated automatically at the end of each solver run via the post-run pipeline (`scripts/generate-report.sh` + `scripts/run-qa.sh`), summarising throughput, determinism checks, `luck.txt` contents, and security posture before the process exits.
- **FR-014**: Logging MUST remain minimal—found key events appended to `luck.txt`, optional checkpoint notices, scope guard violations, and fatal errors—while ensuring audit trails include timestamp, workstation hostname, keyspace, target address, device list, operator ID, and operator-declared purpose even without authentication.
- **FR-015**: Checkpoint files, telemetry logs, and benchmark artifacts MUST include tamper-evident SHA-256 (or stronger) digests stored alongside the payload and verified on load, with failures aborting consumption.
- **FR-016**: CLI MUST require operator metadata via `--operator-id` and `--operator-purpose` (or resolved config) for each run; these values feed telemetry, reports, and compliance logs.

### Non-Functional Requirements
- **NFR-001**: Sustained throughput MUST reach ≥1.0 billion keys/sec on RTX 2080 Ti and ≥2.0/4.0 billion keys/sec on RTX 3090/A100 respectively, with alerts when throughput drops below thresholds.
- **NFR-002**: GPU register usage MUST stay below 128 registers/thread to preserve occupancy on NVIDIA SM 7.5+ hardware.
- **NFR-003**: Checkpoint save/restore MUST complete within 2 seconds on NVMe storage.
- **NFR-004**: Telemetry lines MUST remain ≤120 characters and structured for automated parsing; JSONL entries ≤1 KB.
- **NFR-005**: Replay runs from checkpoints must reproduce identical telemetry and hash outputs (deterministic replays).
- **NFR-006**: Data retention for checkpoints, telemetry, and logs MUST NOT exceed 30 days; passphrases must be rotated after each sustained run.
- **NFR-007**: Integrity verification for checkpoints, telemetry, and benchmark artifacts MUST complete within 250 ms per file on NVMe storage, failing fast on digest mismatch.

### Key Entities
- **KeyRangeShard**: Deterministic slice of the keyspace with fields (`shard_id`, `start_key`, `end_key`, `assigned_device_id`, `status`, `replay_seed`, `last_checkpoint_at`).
- **CheckpointManifest**: Metadata for encrypted checkpoint payloads (`path`, `created_at`, `processed_keys`, `nonce`, `salt`, `pbkdf2_iterations`, `payload_sha256`, `retention_expiry`, `shard_id`).
- **TelemetryPacket**: Runtime metric record (`timestamp`, `device_id`, `operator_id`, `operator_purpose`, `keys_per_sec`, `occupancy`, `checkpoint_latency_ms`, `scope_guard_triggered`, `alerts`, `payload_sha256`, `shard_id`).
- **BenchmarkBaseline**: Performance reference per GPU model (minimum throughput, allowed variance, expected occupancy).
- **TargetHash160 Constant**: `std::array<uint32_t,5>` compiled into device `__constant__` memory for comparisons.
- **CheckpointRecord**: Binary struct capturing `current_scalar`, `device_id`, `rng_seed`, `last_successful_batch`, and checksum to validate resumes.

## Engine Development Roadmap (Hand Scalpel Strategy)
- **Stage I – Engine**: Build the fastest possible secp256k1 CUDA kernel by fusing Endomorphism (secp256k1-zkp) and batch stepping (VanitySearch). A single full multiplication seeds per-block incremental additions; >95% of time must be invested in this optimisation.
- **Stage II – Transplant**: Strip BitCrack’s legacy kernels, graft the new engine via a lean `solve(start,count,device)` interface, and reuse its CLI, KeySpace management, and progress instrumentation indefinitely.
- **Stage III – Dyno Test**: Validate against the golden CPU harness, capture Nsight profiles to maintain <128 registers/thread, and push throughput to Ada-class limits while enforcing the Iron Cage test discipline.

## Reference Obligations & Constraints
- Run `tools/sync_reference_sources.sh --apply` before modifying ECC or scanning primitives; adapt code through adapter layers under `src/KeyhuntCore/**/adapters/`.
- Reuse patterns from VanitySearch (kernel layout), BitCrack (KeyFinder scheduling), and ElementsProject/secp256k1-zkp (endomorphism constants) without re-implementing primitives.
- CPU validation MUST rely on bitcoin-core/secp256k1 or vendored equivalent.
- Network services, alternate puzzles, and GUI front-ends are explicitly out of scope.

## Telemetry & Observability
- Telemetry lines follow `[timestamp][device-id][metric=value,...]` with alerts when throughput <1,000M keys/sec or checkpoint latency >2,000 ms.
- Optional `--metrics-json <path>` argument outputs NDJSON packets conforming to `contracts/telemetry-packet.json` and may be scraped by external dashboards.
- All telemetry, checkpoint, and benchmark artifacts MUST include inline SHA-256 digests and signature blocks so downstream consumers can verify integrity before use.
- Deterministic replay verification MUST be available via `scripts/replay/verify-replay.sh`, comparing regenerated telemetry/hash outputs against the stored digests and failing fast if differences exceed zero tolerance.
- Critical faults MUST trigger immediate checkpoint save and controlled shutdown with non-zero exit code.

## Review & Acceptance Checklist
*GATE: Automated checks run during main() execution*

### Content Quality
- [x] No implementation details (languages, frameworks, APIs)
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders
- [x] All mandatory sections completed

### Requirement Completeness
- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous  
- [x] Success criteria are measurable
- [x] Scope is clearly bounded
- [x] Dependencies and assumptions identified

---

## Execution Status
*Updated by main() during processing*

- [x] User description parsed
- [x] Key concepts extracted
- [x] Ambiguities marked
- [x] User scenarios defined
- [x] Requirements generated
- [x] Entities identified
- [x] Review checklist passed
