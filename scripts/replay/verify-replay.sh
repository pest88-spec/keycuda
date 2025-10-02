#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

if [[ $# -lt 2 ]]; then
  echo "Usage: $(basename "$0") <manifest> <telemetry-jsonl>" >&2
  exit 1
fi

MANIFEST="$1"
TELEMETRY="$2"

if [[ ! -f "$MANIFEST" ]]; then
  echo "Manifest not found: $MANIFEST" >&2
  exit 2
fi

if [[ ! -f "$TELEMETRY" ]]; then
  echo "Telemetry file not found: $TELEMETRY" >&2
  exit 3
fi

MANIFEST_DIR="$(cd "$(dirname "$MANIFEST")" && pwd)"

MANIFEST_INFO=$(PUZZLE71_MANIFEST="$MANIFEST" python3 <<'PY'
import json
import os
from pathlib import Path

manifest_path = Path(os.environ['PUZZLE71_MANIFEST'])
with manifest_path.open('r', encoding='utf-8') as fh:
    data = json.load(fh)

payload_rel = data.get('path', '')
payload_sha = data.get('payload_sha256', '')
processed_keys = data.get('processed_keys', '')

if not payload_rel or not payload_sha:
    raise SystemExit('Manifest missing payload path or SHA-256 field')

print(payload_rel)
print(payload_sha)
print(processed_keys)
PY
)

readarray -t MANIFEST_FIELDS <<<"$MANIFEST_INFO"
PAYLOAD_REL="${MANIFEST_FIELDS[0]}"
PAYLOAD_SHA="${MANIFEST_FIELDS[1]}"
PROCESSED_KEYS="${MANIFEST_FIELDS[2]}"

PAYLOAD_PATH="$PAYLOAD_REL"
if [[ ! "$PAYLOAD_PATH" = /* ]]; then
  PAYLOAD_PATH="${MANIFEST_DIR}/${PAYLOAD_REL}"
fi

if [[ ! -f "$PAYLOAD_PATH" ]]; then
  echo "Payload referenced in manifest is missing: $PAYLOAD_PATH" >&2
  exit 4
fi

CALCULATED_SHA=$(PUZZLE71_PAYLOAD="$PAYLOAD_PATH" python3 <<'PY'
import hashlib
import os
from pathlib import Path

payload_path = Path(os.environ['PUZZLE71_PAYLOAD'])

sha256 = hashlib.sha256()
with payload_path.open('rb') as fh:
    for chunk in iter(lambda: fh.read(1024 * 1024), b''):
        sha256.update(chunk)
print(sha256.hexdigest())
PY
)

if [[ "$CALCULATED_SHA" != "$PAYLOAD_SHA" ]]; then
  echo "Digest mismatch for payload" >&2
  echo " Expected: $PAYLOAD_SHA" >&2
  echo "   Actual: $CALCULATED_SHA" >&2
  exit 5
fi

if [[ ! -s "$TELEMETRY" ]]; then
  echo "Telemetry file is empty: $TELEMETRY" >&2
  exit 6
fi

TELEMETRY_SHA=$(PUZZLE71_TELEMETRY="$TELEMETRY" python3 <<'PY'
import hashlib
import os
from pathlib import Path

telemetry_path = Path(os.environ['PUZZLE71_TELEMETRY'])
sha256 = hashlib.sha256()
with telemetry_path.open('rb') as fh:
    for chunk in iter(lambda: fh.read(1024 * 1024), b''):
        sha256.update(chunk)
print(sha256.hexdigest())
PY
)

cat <<EOF
Replay verification completed successfully.
Manifest : $MANIFEST
Payload  : $PAYLOAD_PATH
Telemetry: $TELEMETRY
Processed keys: $PROCESSED_KEYS
Payload SHA-256 verified: $PAYLOAD_SHA
Telemetry SHA-256: $TELEMETRY_SHA
EOF
