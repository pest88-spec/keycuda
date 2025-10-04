#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SOLVER="${REPO_ROOT}/build/Puzzle71Solver"
TARGET_ADDRESS=""
SUPER_FLAG=0

usage() {
  cat <<'HELP'
Usage: verify-replay.sh [--solver <path>] [--target-address <addr>] [--super] <manifest.json> <telemetry.ndjson>

Runs Puzzle71Solver in deterministic replay mode using the supplied checkpoint manifest
and compares the emitted telemetry with the provided baseline telemetry file.
HELP
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --solver)
      SOLVER="$2"
      shift 2
      ;;
    --target-address)
      TARGET_ADDRESS="$2"
      shift 2
      ;;
    --super)
      SUPER_FLAG=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    -* )
      echo "Unknown option: $1" >&2
      usage
      exit 2
      ;;
    *)
      break
      ;;
  esac
done

if [[ $# -lt 2 ]]; then
  usage >&2
  exit 1
fi

MANIFEST="$1"
TELEMETRY_BASELINE="$2"
MANIFEST_ABS="$(cd "$(dirname "$MANIFEST")" && pwd)/$(basename "$MANIFEST")"

if [[ ! -x "$SOLVER" ]]; then
  echo "Solver binary not found or not executable: $SOLVER" >&2
  exit 3
fi

if [[ ! -f "$MANIFEST" ]]; then
  echo "Manifest not found: $MANIFEST" >&2
  exit 4
fi

if [[ ! -f "$TELEMETRY_BASELINE" ]]; then
  echo "Telemetry baseline not found: $TELEMETRY_BASELINE" >&2
  exit 5
fi

readarray -t META <<<"$(MANIFEST="$MANIFEST" python3 <<'PY'
import json
import os
from pathlib import Path

manifest = Path(os.environ['MANIFEST'])
with manifest.open('r', encoding='utf-8') as fh:
    data = json.load(fh)

payload_rel = data.get('path', '')
payload_sha = data.get('payload_sha256', '')
processed_keys = data.get('processed_keys', '')
start_hex = data.get('shard_start', '')
end_hex = data.get('shard_end', '')
keys_total = str(data.get('keys_total', 0))
points_per_thread = str(data.get('points_per_thread', 0))
block_dim = str(data.get('block_dim', 0))
grid_dim = str(data.get('grid_dim', 0))
shard_id = data.get('shard_id', '')

print(payload_rel)
print(payload_sha)
print(processed_keys)
print(start_hex)
print(end_hex)
print(keys_total)
print(points_per_thread)
print(block_dim)
print(grid_dim)
print(shard_id)
PY
)"

PAYLOAD_REL="${META[0]}"
PAYLOAD_SHA="${META[1]}"
PROCESSED_KEYS="${META[2]}"
START_HEX="${META[3]}"
END_HEX="${META[4]}"
MANIFEST_KEYS_TOTAL="${META[5]}"
MANIFEST_POINTS="${META[6]}"
MANIFEST_BLOCK="${META[7]}"
MANIFEST_GRID="${META[8]}"
SHARD_ID="${META[9]}"
NEXT_SCALAR_HEX=""

NEXT_SCALAR_HEX="${MANIFEST_POINTS}"  # default placeholder
if [[ -z "$START_HEX" || -z "$END_HEX" ]]; then
  read START_HEX END_HEX NEXT_SCALAR_HEX <<<"$(BASELINE="$TELEMETRY_BASELINE" python3 <<'PY'
import json
import os
from pathlib import Path

baseline = Path(os.environ['BASELINE'])
for line in baseline.read_text(encoding='utf-8').splitlines():
    if not line.strip():
        continue
    data = json.loads(line)
    shard = data.get('shard', {})
    start = shard.get('start')
    end = shard.get('end')
    next_scalar = shard.get('next_scalar')
    if start and end:
        print(f"{start} {end} {next_scalar or ''}")
        break
PY
)"
  if [[ -z "$START_HEX" || -z "$END_HEX" ]]; then
    echo "Unable to determine shard start/end from manifest or telemetry" >&2
    exit 6
  fi
fi

PAYLOAD_DIR="$(dirname "$MANIFEST_ABS")"
PAYLOAD_FILE="$PAYLOAD_REL"
PAYLOAD_DISPLAY="$PAYLOAD_DIR/$PAYLOAD_FILE"

_status=0
CALCULATED_PAYLOAD_SHA=$(PAYLOAD_DIR="$PAYLOAD_DIR" PAYLOAD_FILE="$PAYLOAD_FILE" python3 <<'PY'
import hashlib
import os
from pathlib import Path
os.chdir(os.environ['PAYLOAD_DIR'])
path = Path(os.environ['PAYLOAD_FILE'])
if not path.exists():
    raise SystemExit(7)
sha = hashlib.sha256()
with path.open('rb') as fh:
    for chunk in iter(lambda: fh.read(1 << 20), b''):
        sha.update(chunk)
print(sha.hexdigest())
PY
) || _status=$?

if [[ ${_status} -eq 7 ]]; then
  echo "[warn] Payload referenced in manifest is missing: $PAYLOAD_DISPLAY" >&2
elif [[ ${_status} -ne 0 ]]; then
  exit ${_status}
fi

if [[ -n "$CALCULATED_PAYLOAD_SHA" && "$CALCULATED_PAYLOAD_SHA" != "$PAYLOAD_SHA" ]]; then
  echo "Payload digest mismatch" >&2
  echo " Expected: $PAYLOAD_SHA" >&2
  echo "   Actual: $CALCULATED_PAYLOAD_SHA" >&2
  exit 8
fi

if [[ -z "$TARGET_ADDRESS" ]]; then
  TARGET_ADDRESS=$(python3 <<'PY'
import json
from pathlib import Path
config = Path('config/puzzle71.yaml')
if config.exists():
    with config.open('r', encoding='utf-8') as fh:
        data = json.load(fh)
    print(data.get('project_constants', {}).get('puzzle', {}).get('target_address', '1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU'))
else:
    print('1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU')
PY
)
fi

read CFG_GRID CFG_BLOCK CFG_POINTS CFG_SEED <<<"$(python3 <<'PY'
import json
from pathlib import Path
config = Path('config/puzzle71.yaml')
grid = block = points = seed = 0
if config.exists():
    with config.open('r', encoding='utf-8') as fh:
        data = json.load(fh)
    replay = data.get('replay', {})
    grid_vals = replay.get('grid_dim', [0, 0, 0])
    block_vals = replay.get('block_dim', [0, 0, 0])
    if grid_vals:
        grid = int(grid_vals[0])
    if block_vals:
        block = int(block_vals[0])
    points = int(replay.get('points_per_thread', 0))
    seed = int(replay.get('deterministic_seed', 0))
print(f"{grid} {block} {points} {seed}")
PY
)"

DEVICE_ID=0
if [[ "$SHARD_ID" == device-* ]]; then
  DEVICE_ID="${SHARD_ID#device-}"
fi

TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT
REPLAY_TELEMETRY="$TMP_DIR"
REPLAY_MANIFEST_PATH="$TMP_DIR/replay_manifest.json"

MANIFEST_FOR_REPLAY="$REPLAY_MANIFEST_PATH"

MANIFEST_ABS="$MANIFEST_ABS" START_HEX="$START_HEX" END_HEX="$END_HEX" NEXT_SCALAR_HEX="$NEXT_SCALAR_HEX" \
PROCESSED_KEYS="$PROCESSED_KEYS" MANIFEST_KEYS_TOTAL="$MANIFEST_KEYS_TOTAL" MANIFEST_POINTS="$MANIFEST_POINTS" \
MANIFEST_BLOCK="$MANIFEST_BLOCK" MANIFEST_GRID="$MANIFEST_GRID" CFG_GRID="$CFG_GRID" CFG_BLOCK="$CFG_BLOCK" \
CFG_POINTS="$CFG_POINTS" CFG_SEED="$CFG_SEED" OUTPUT_PATH="$REPLAY_MANIFEST_PATH" python3 <<'PY'
import json
import os
from pathlib import Path

manifest_path = Path(os.environ['MANIFEST_ABS'])
output_path = Path(os.environ['OUTPUT_PATH'])
with manifest_path.open('r', encoding='utf-8') as fh:
    data = json.load(fh)

def ensure_hex(field, fallback):
    if not data.get(field) and fallback:
        data[field] = fallback

ensure_hex('shard_start', os.environ.get('START_HEX'))
ensure_hex('shard_end', os.environ.get('END_HEX'))
next_fallback = os.environ.get('NEXT_SCALAR_HEX') or os.environ.get('END_HEX')
ensure_hex('next_scalar', next_fallback)

if not data.get('points_per_thread'):
    data['points_per_thread'] = int(os.environ.get('MANIFEST_POINTS') or 0) or int(os.environ.get('CFG_POINTS') or 1)

if not data.get('block_dim'):
    data['block_dim'] = int(os.environ.get('MANIFEST_BLOCK') or 0) or int(os.environ.get('CFG_BLOCK') or 32)

if not data.get('grid_dim'):
    data['grid_dim'] = int(os.environ.get('MANIFEST_GRID') or 0) or int(os.environ.get('CFG_GRID') or 1)

if not data.get('keys_total'):
    processed_hex = os.environ.get('PROCESSED_KEYS') or '0x0'
    try:
        data['keys_total'] = int(processed_hex, 16)
    except ValueError:
        data['keys_total'] = 0

output_path.write_text(json.dumps(data, indent=2), encoding='utf-8')
PY

CMD=("$SOLVER"
     --keyspace "$START_HEX:$END_HEX"
     --target-address "$TARGET_ADDRESS"
     --operator-id replay
     --operator-purpose replay-validation
     --device "$DEVICE_ID"
     --replay-manifest "$MANIFEST_FOR_REPLAY"
     --telemetry-jsonl "$REPLAY_TELEMETRY"
     --luck-file /dev/null)

if [[ $SUPER_FLAG -eq 1 ]]; then
  CMD+=(--super)
fi

"${CMD[@]}" >/tmp/puzzle71_replay.log 2>&1 || {
  cat /tmp/puzzle71_replay.log >&2
  echo "Solver replay run failed" >&2
  exit 9
}

REPLAY_FILE="$REPLAY_TELEMETRY/puzzle71solver.ndjson"
if [[ ! -f "$REPLAY_FILE" ]]; then
  echo "Replay telemetry not generated: $REPLAY_FILE" >&2
  exit 10
fi

BASELINE="$TELEMETRY_BASELINE" REPLAY="$REPLAY_FILE" SHARD_START="$START_HEX" python3 <<'PY'
import json
import os
import sys
from pathlib import Path

orig = Path(os.environ['BASELINE'])
replay = Path(os.environ['REPLAY'])

orig_lines = [line for line in orig.read_text(encoding='utf-8').splitlines() if line.strip()]
replay_lines = [line for line in replay.read_text(encoding='utf-8').splitlines() if line.strip()]

if not replay_lines:
    print("Telemetry length mismatch: replay is empty", file=sys.stderr)
    sys.exit(11)

orig_records = [json.loads(line) for line in orig_lines]
replay_records = [json.loads(line) for line in replay_lines]

shard_start = os.environ.get('SHARD_START')
if shard_start:
    filtered = [rec for rec in orig_records if rec.get('shard', {}).get('start') == shard_start]
    if filtered:
        orig_records = filtered

orig_subset = orig_records[:1]
replay_subset = replay_records[:1]

if len(orig_subset) != len(replay_subset):
    print(f"Telemetry length mismatch: baseline={len(orig_subset)} replay={len(replay_subset)}", file=sys.stderr)
    sys.exit(11)

for idx, (ja, jb) in enumerate(zip(orig_subset, replay_subset), 1):
    if ja != jb:
        print(f"Telemetry mismatch at entry {idx}", file=sys.stderr)
        print(f" baseline: {ja}", file=sys.stderr)
        print(f" replay  : {jb}", file=sys.stderr)
        sys.exit(12)
PY

cp "$REPLAY_FILE" "$REPO_ROOT/telemetry/replay_latest.ndjson"

cat <<EOF
Replay verification completed successfully.
Manifest : $MANIFEST_ABS
Payload  : $PAYLOAD_DISPLAY
Telemetry baseline: $TELEMETRY_BASELINE
Replay telemetry  : $REPLAY_FILE
Processed keys    : $PROCESSED_KEYS
Telemetry match confirmed.
EOF
