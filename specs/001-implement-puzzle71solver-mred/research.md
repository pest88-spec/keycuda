# Research: Puzzle71Solver CUDA Implementation (Phase 0)

## Deterministic Kernel Replay Flow
- **Decision**: Standardise launch configuration via config file (`puzzle71.yaml`) capturing grid/block sizes, points-per-thread, and deterministic seed; expose CLI `--replay-checkpoint <path>` to run single-shard replay with CPU verification enabled.
- **Rationale**: Replaying from recorded checkpoints with identical launch parameters proves GPU/CPU parity and satisfies Constitution Principle I.
- **Alternatives Considered**:
  - Derive parameters at runtime from hardware query → rejected; non-deterministic scaling hides regression causes.
  - Embed launch settings in binary → rejected; reduces flexibility for higher-tier GPUs and complicates audits.
- **Follow-up**: Define config schema in data model; add quickstart replay steps.

## Checkpoint Encryption Algorithm
- **Decision**: Use AES-256-GCM with per-checkpoint random nonce, keys derived from operator-provided passphrase via PBKDF2-HMAC-SHA512 (200k iterations).
- **Rationale**: AES-GCM supported by existing tooling, provides authenticated encryption, and aligns with Constitution Principle IV for tamper evidence.
- **Alternatives Considered**:
  - ChaCha20-Poly1305 → strong candidate but increases integration work with current BitCrack-style utilities.
  - AES-CTR + HMAC → more implementation complexity without additional benefit over GCM.
- **Follow-up**: Document key derivation prompt in quickstart; include nonce + salt in checkpoint manifest.

## Telemetry Sink & Alerting
- **Decision**: Emit structured NDJSON logs (`telemetry/*.jsonl`) plus optional Prometheus textfile exporter for operators with monitoring stacks.
- **Rationale**: JSONL is easy to replay and archive; textfile exporter integrates with node exporters without requiring persistent daemons, meeting Principle III.
- **Alternatives Considered**:
  - Direct Prometheus pushgateway → rejected to avoid external dependency and network requirements.
  - Plaintext logs → insufficient structure for automated alerting.
- **Follow-up**: Define telemetry packet schema; specify alert thresholds (throughput <1,000M keys/sec, checkpoint latency >2s, scope guard triggered).

## Performance Benchmarking Protocol
- **Decision**: Run 3 warm-up batches followed by 5 measurement batches per GPU; compute median keys/sec and compare to baseline table (2080 Ti ≥1,000M, 3090 ≥2,000M, A100 ≥4,000M). Capture occupancy metrics via `nvprof` or `Nsight` summary dump.
- **Rationale**: Multiple measurements mitigate jitter, documenting reproducible performance for Principle II/III compliance.
- **Alternatives Considered**:
  - Single long-duration run → less reproducible due to thermal throttling.
  - Synthetic microbenchmarks only → fails to capture integration overhead (I/O, verification).
- **Follow-up**: Add benchmark instructions to quickstart and tasks to integrate into CI/perf scripts.
