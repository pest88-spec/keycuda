# Checkpoints Directory

This directory contains checkpoint files for puzzle solving progress persistence and deterministic replay validation.

## File Types

### Manifest Files (`manifest-*.json`)
JSON metadata files containing:
- Shard range (start/end scalars)
- Grid/block dimensions
- Keys processed count
- Encryption metadata (cipher, salt, nonce)
- Payload SHA-256 checksum

### Payload Files (`payload-*.chk`)
Binary encrypted checkpoint data containing:
- GPU state snapshots
- Private key positions
- Chain computation state

## Naming Convention

Format: `{type}-{start_scalar}-{timestamp}.{ext}`

Example: `manifest-0xa00-2025-09-28T232228Z.json`

**Note**: Timestamps use compact format (HHMMSS) without colons for Windows filesystem compatibility.

## Example Files

The committed files serve as:
- Test fixtures for replay validation scripts
- Reference format examples
- Integration test data

## Runtime Behavior

New checkpoint files generated during solver execution are automatically ignored by git (see `.gitignore`).

To create a checkpoint manually:
```bash
./Puzzle71Solver --keyspace 0xa00:0xbff --checkpoint-interval 1000000000
```

## Replay Validation

To verify deterministic execution:
```bash
./scripts/replay/verify-replay.sh \
    checkpoints/manifest-0xa00-2025-09-28T232228Z.json \
    telemetry/baseline.ndjson
```
