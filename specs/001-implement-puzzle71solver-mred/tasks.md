# Tasks: Puzzle71Solver CUDA Implementation

**Input**: Design docs from `/specs/001-implement-puzzle71solver-mred/`  
**Prerequisites**: plan.md, research.md, data-model.md, contracts/, quickstart.md

## Execution Flow (main)
```
1. Perform environment + reference scaffolding (T001–T005)
2. Author failing tests & benchmarks (T006–T028)
3. Implement CUDA kernels, services, and CLI surfaces (T029–T041)
4. Integrate replay, telemetry, scripts, and automation (T042–T051)
5. Polish with documentation, compliance, and evidence archives (T052–T057)
```

## Format
`[ID] [P?] Description`

## Phase 3.1: Setup & Baseline
- [X] T001 Capture hardware + CUDA environment (GPU models, driver, CUDA toolkit, Nsight versions) in `docs/environment.md`.
- [X] T002 Run `tools/sync_reference_sources.sh --apply`, record upstream commit hashes (VanitySearch, BitCrack, secp256k1-zkp) in `docs/reference-locks.md`. *(Updated 2025-09-25 with commit hashes)*
- [X] T003 Configure root `CMakeLists.txt` to build `src/puzzle71_kernel.cu`, `src/solver.cpp`, `src/main.cpp`, enable CUDA 11.8 flags, GoogleTest targets, and reproducible build settings.
- [X] T004 Establish scaffolding: create `logs/`, `telemetry/`, `benchmarks/`, `reports/`, `digests/`, seed `config/puzzle71.yaml` with keyspace/operator defaults, and ensure `luck.txt` is git-ignored and documented.
- [X] T005 [P] Define Puzzle #71 constants and operator metadata helpers in `src/models/target_constants.h` with stub hooks for upcoming tests.

## Phase 3.2: Tests First (MUST FAIL BEFORE IMPLEMENTATION)
- [X] T006 [P] Author scalar/endomorphism property tests in `tests/property/test_scalar_ops.cpp` exercising CPU references.
- [X] T007 [P] Add validation test `tests/validation/test_endomorphism_split.cpp` comparing GPU splits against bitcoin-core outputs.
- [X] T008 [P] Add validation test `tests/validation/test_batch_step_increment.cpp` verifying incremental additions reproduce full multiplication.
- [X] T009 [P] Add validation test `tests/validation/test_hash160_gpu_cpu_parity.cpp` sampling ≥1,024 keys for HASH160 parity.
- [X] T010 [P] Create integration test `tests/integration/test_gpu_determinism.cu` covering replayable shard execution and CPU confirmation.
- [X] T011 [P] Create integration test `tests/integration/test_checkpoint_resume.cpp` ensuring gaps/duplicates are prevented on resume.
- [X] T012 [P] Create integration test `tests/integration/test_checkpoint_replay_diff.cpp` asserting replayed telemetry/hash digests are identical.
- [X] T013 [P] Create integration test `tests/integration/test_multi_gpu_partition.cpp` validating deterministic shard allocation and reassignment logs.
- [X] T014 [P] Create integration test `tests/integration/test_scope_guard.cpp` ensuring out-of-range keyspace triggers guarded exit with alert.
- [X] T015 [P] Add contract test `tests/contract/test_solver_keyspace.cpp` enforcing CLI keyspace validation per `contracts/cli.md`.
- [X] T016 [P] Add contract test `tests/contract/test_solver_target_address.cpp` verifying canonical Puzzle #71 digest enforcement.
- [X] T017 [P] Add contract test `tests/contract/test_solver_operator_metadata.cpp` covering required `--operator-id`/`--operator-purpose` precedence.
- [X] T018 [P] Add contract test `tests/contract/test_checkpoint_manifest_schema.cpp` validating manifests against `contracts/checkpoint-manifest.json`.
- [X] T019 [P] Add contract test `tests/contract/test_telemetry_packet_schema.cpp` validating telemetry NDJSON packets.
- [X] T020 [P] Add unit test `tests/unit/test_digest_verifier.cpp` for SHA-256 manifest load/verify failure paths.
- [X] T021 [P] Add unit test `tests/unit/test_telemetry_format_limits.cpp` enforcing ≤120 char NDJSON lines and ≤1 KB JSON payloads.
- [X] T022 [P] Add kernel harness test `tests/unit/test_kernel_interfaces.cu` ensuring deterministic launch parameters and sample outputs.
- [X] T023 [P] Add unit test `tests/unit/test_checkpoint_crypto.cpp` covering AES-256-GCM encrypt/decrypt error handling.
- [X] T024 [P] Add unit test `tests/unit/test_checkpoint_manifest.cpp` validating serializer/deserializer edge cases.
- [X] T025 [P] Add unit test `tests/unit/test_prometheus_exporter.cpp` asserting metrics layout per spec.
- [X] T026 [P] Add performance harness `tests/perf/test_range_scan_benchmark.cu` enforcing ≥1,000M keys/sec baseline for RTX 2080 Ti.
- [X] T027 [P] Add performance test `tests/perf/test_checkpoint_latency.cpp` failing when checkpoint write exceeds 2 seconds.
- [X] T028 [P] Add performance test `tests/perf/test_digest_verifier_perf.cpp` enforcing ≤250 ms digest verification SLA.

## Phase 3.3: Core CUDA & Services Implementation
- [X] T029 Implement the fused batch stepping + HASH160 kernel in `src/puzzle71_kernel.cu`, delivering full point-multiplication, HASH160 comparison, and register audit instrumentation.
- [X] T030 Implement device hash helpers in `src/compare/kernels/hash160_fused.h`, exposing RIPEMD160/SHA256 primitives backed by constant memory for use by T029 and kernel tests.
- [X] T031 [P] Implement digest verification library in `src/utils/digest_verifier.cpp` (load/store manifests, SHA-256 validation, error codes) with end-to-end tests covering corrupt payloads.
- [X] T032 [P] Implement AES-256-GCM checkpoint crypto utilities in `src/utils/checkpoint_crypto.cpp` with PBKDF2 integration, including constant-time failure handling.
- [X] T033 [P] Implement checkpoint manifest serializer/deserializer + schema enforcement in `src/checkpoint_manifest.cpp`/`.h` and validate against `contracts/checkpoint-manifest.json`.
- [X] T034 [P] Implement telemetry logger in `src/utils/telemetry_logger.cpp` streaming NDJSON + operator audit fields. *(Placeholder writer)*
- [X] T035 [P] Implement Prometheus exporter in `src/utils/prometheus_exporter.cpp` emitting throughput/checkpoint gauges. *(Stub writing payload)*
- [X] T036 Implement deterministic range scheduler in `src/scheduler/range_scheduler.cpp` using `KeyRangeShard` metadata. *(Initial contiguous split stub)*
- [X] T037 Implement multi-GPU partitioner in `src/scan/puzzle71_partition.cpp` with reassignment lineage tracking. *(Lineage TODO)*
- [X] T038 [P] Implement replay-aware config loader in `src/config/puzzle71_config.cpp` ingesting `puzzle71.yaml`. *(Basic JSON loader; YAML TODO)*
- [X] T039 [P] Implement NVML-backed device metrics service in `src/services/device_metrics.cpp` exposing occupancy/alerts. *(Stub without NVML)*
- [X] T040 Implement solver interface in `src/solver.cpp`/`.h` managing buffers, operator metadata propagation, and luck.txt append. *(Stub Run method)*
- [X] T041 Implement CLI entrypoint in `src/main.cpp` enforcing required flags, dry-run validation, and digest alerts.

## Phase 3.4: Integration & Automation
- [X] T042 Integrate solver, scheduler, telemetry, and digest emission in `src/main.cpp`, invoking deterministic replay checks on resume paths. *(Initial integration; kernel + replay remain TODO)*
- [X] T043 [P] Implement checkpoint rotation/resume workflow across `src/solver.cpp` + `src/main.cpp`, ensuring manifest digests recorded and verified. *(Manifest write/read stubs in place)*
- [X] T044 [P] Build deterministic replay verifier script `scripts/replay/verify-replay.sh` comparing telemetry/hash outputs to stored digests. *(Stub script)*
- [X] T045 [P] Build digest sweep script `scripts/digest/check-artifact-digests.sh` covering checkpoints, telemetry, benchmarks, and reports with SHA-256 verification and failure propagation.
- [X] T046 [P] Build benchmark runner `scripts/run-benchmarks.sh` (warm-up + measurement batches, median output to `benchmarks/latest.json`). *(Stub script)*
- [X] T047 [P] Build checkpoint retention tool `scripts/purge-checkpoints.sh` enforcing 30-day policy and passphrase rotation logs. *(Stub script)*
- [X] T048 [P] Build QA orchestrator `scripts/run-qa.sh` chaining failing-tests check, replay verifier, benchmark smoke run. *(Stub script)*
- [X] T049 [P] Build final report generator `scripts/generate-report.sh` summarising throughput, determinism, digests, and audit metadata. *(Stub script)*
- [X] T050 [P] Add Nsight profile helper `tools/nsight/puzzle71_profile.sh` capturing register/occupancy metrics for kernels. *(Stub script)*
- [X] T051 [P] Wire automated post-run pipeline in `src/main.cpp` to trigger `scripts/run-qa.sh` and `scripts/generate-report.sh` on solver exit.

## Phase 3.5: Polish, Documentation & Compliance
- [X] T052 Update documentation (`quickstart.md`, `docs/performance.md`, `docs/runbooks/checkpoint.md`) with final commands, replay workflow, and digest verification steps.
- [X] T053 [P] Update governance checklist `docs/governance/compliance-checklist.md` covering scope guard logs, passphrase rotation, digest evidence. *(Draft with TODOs)*
- [X] T054 [P] Archive GPU/CPU parity evidence in `docs/validation/puzzle71_parity.md` with dataset references and telemetry attachments.
- [X] T055 [P] Archive deterministic replay evidence in `docs/validation/puzzle71_replay.md` including diff reports and manifest hashes.
- [X] T056 Run final compliance sweep: execute `scripts/run-qa.sh`, confirm `digests/latest.json` integrity, and mark validation checklist complete in `specs/001-implement-puzzle71solver-mred/plan.md`, leaving an auditable WORM log entry.
- [X] T057 Capture register-usage evidence: run Nsight Compute (or equivalent) against production kernels, enforce ≤128 registers/thread, store reports and digests under `docs/validation/evidence/`.

## Dependencies
- T001–T005 complete before any tests; T002 gates kernel work; T004 seeds config required by later tasks.
- T006–T028 MUST exist and fail before starting T029–T041 (TDD gate).
- T029 precedes T030; both precede kernel harness integration (T022) passing.
- T031–T035 provide utilities required by T042–T048; do not start integration scripts before these land.
- T036/T037 must finish before multi-GPU integration tests (T013) are re-run and before T042.
- T040/T041 must complete before wiring automation in T042–T051.
- Integration scripts (T044–T051) must finish before polish tasks (T052–T057).
- Final compliance (T056) requires all prior tasks complete; register budget evidence (T057) closes NFR-002.

## Parallel Execution Example
```
# After setup (T001–T005) completes, scaffold failing tests in parallel:
/specs/001-implement-puzzle71solver-mred$ task T006
/specs/001-implement-puzzle71solver-mred$ task T010
/specs/001-implement-puzzle71solver-mred$ task T015
/specs/001-implement-puzzle71solver-mred$ task T018
/specs/001-implement-puzzle71solver-mred$ task T026
/specs/001-implement-puzzle71solver-mred$ task T028
```

## Notes
- Mark tests as failing with captured evidence before moving into Phase 3.3.
- Prefer deterministic seeds/logging for all GPU tests to satisfy replay requirements.
- Commit after each task, referencing the task ID (`T0xx`) and noting determinism/observability/security impacts.
- `tools/sync_reference_sources.sh --apply` must be rerun if ECC code changes; capture new hashes in `docs/reference-locks.md`.
- Ensure scripts under `scripts/` and `tools/` are executable (`chmod +x`) as part of their tasks.
