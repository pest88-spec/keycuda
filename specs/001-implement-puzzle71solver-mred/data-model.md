# Data Model: Puzzle71Solver

## Entities

### KeyRangeShard
| Field | Type | Description | Validation |
|-------|------|-------------|------------|
| shard_id | string (UUID) | Unique identifier for the shard assignment | Required; immutable once issued |
| start_key | uint256 (hex string) | Inclusive starting private key value | Must be ≥ ProjectConstants.PrivateKeySpace.Start |
| end_key | uint256 (hex string) | Inclusive ending private key value | Must be ≤ ProjectConstants.PrivateKeySpace.End and > start_key |
| assigned_device_id | string | GPU device identifier (e.g., `GPU0`, `GPU1`) or `CPU` fallback | Required |
| status | enum {queued, running, completed, failed, reassigned} | Execution lifecycle state | Transitions: queued→running→completed; running→failed/reassigned |
| replay_seed | uint64 | Deterministic seed recorded for replaying batch stepping | Required for reproducibility |
| last_checkpoint_at | timestamp | Most recent checkpoint persisted for this shard | Optional; null until first checkpoint |

**Relationships**: CheckpointManifest references zero or one KeyRangeShard; TelemetryPacket references many. Reassigned shards create new `shard_id` entries linked via audit log.

### CheckpointManifest
| Field | Type | Description | Validation |
|-------|------|-------------|------------|
| path | string | Filesystem path to encrypted checkpoint payload | Must exist before resume |
| created_at | timestamp (ISO 8601) | Creation time | Required |
| processed_keys | uint128 | Count of keys scanned prior to checkpoint | Must be multiple of batch size |
| encryption_cipher | enum {AES-256-GCM} | Cipher used for payload | Required |
| nonce | base64(12 bytes) | Nonce used for GCM | Required |
| salt | base64(16 bytes) | PBKDF2 salt | Required |
| pbkdf2_iterations | int | Iterations used for key derivation | ≥200000 |
| hash160_digest | hex(40) | HASH160 digest verifying checkpoint integrity | Required |
| retention_expiry | timestamp | Time when checkpoint must be deleted | ≤ created_at + 30 days |
| shard_id | string | FK to KeyRangeShard | Required |

### TelemetryPacket
| Field | Type | Description | Validation |
|-------|------|-------------|------------|
| timestamp | timestamp | Event time | Required |
| device_id | string | GPU or CPU identifier | Required |
| keys_per_sec | float | Processed keys per second for the interval | ≥0; alert if <1,000M for GPU shards |
| occupancy | float (0-100) | SM occupancy percentage | Required |
| checkpoint_latency_ms | float | Time to write checkpoint in ms | Alert if >2000 |
| scope_guard_triggered | bool | Whether out-of-range guard fired | Required |
| alerts | array[string] | Active alert identifiers | Optional (empty array default) |
| shard_id | string | FK to KeyRangeShard | Required |

### BenchmarkBaseline
| Field | Type | Description |
|-------|------|-------------|
| gpu_model | string | Device model string (e.g., "RTX 2080 Ti") |
| min_keys_per_sec | float | Required minimum throughput |
| max_variance_pct | float | Allowed % variance vs baseline |
| reference_occupancy | float | Expected SM occupancy |

## Derived Relationships & Rules
- KeyRangeShard status transitions are logged with timestamp and operator workstation; failed shards auto-generate reassignment entries.
- CheckpointManifest retention enforcement runs after every checkpoint write and during shutdown.
- TelemetryPacket entries feed Prometheus exporter; alerts propagate to CLI.
- BenchmarkBaseline entries inform acceptance criteria and tests for each GPU tier.
