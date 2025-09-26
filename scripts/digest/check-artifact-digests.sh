#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DIGEST_DIR="${REPO_ROOT}/digests"
OUTPUT="${DIGEST_DIR}/latest.json"

ARTIFACT_PATHS=(
  "${REPO_ROOT}/checkpoints"
  "${REPO_ROOT}/telemetry"
  "${REPO_ROOT}/benchmarks"
  "${REPO_ROOT}/reports"
)

declare -A DIGESTS

for path in "${ARTIFACT_PATHS[@]}"; do
  [[ -d "$path" ]] || continue
  while IFS= read -r -d '' file; do
    hash=$(sha256sum "$file" | awk '{print $1}')
    DIGESTS["$file"]="$hash"
  done < <(find "$path" -type f -print0)

done

mkdir -p "$DIGEST_DIR"
echo "{" > "$OUTPUT"
first=1
for file in "${!DIGESTS[@]}"; do
  [[ $first -eq 1 ]] || echo "," >> "$OUTPUT"
  first=0
  printf '  "%s": "%s"\n' "$file" "${DIGESTS[$file]}" >> "$OUTPUT"

done
echo "}" >> "$OUTPUT"

echo "Digest manifest written to $OUTPUT" >&2

manifest_fail=0
while IFS= read -r -d '' manifest; do
  python3 - <<'PY'
import json, hashlib, sys
from pathlib import Path

manifest_path = Path(sys.argv[1])
data = json.loads(manifest_path.read_text())
payload = Path(data.get("path", ""))
expected = data.get("payload_sha256")

if not payload.exists():
    print(f"[warn] Payload missing for manifest {manifest_path}")
    sys.exit(2)

sha = hashlib.sha256(payload.read_bytes()).hexdigest()
if expected and sha != expected:
    print(f"[error] SHA mismatch for {manifest_path}: expected {expected}, got {sha}")
    sys.exit(1)
sys.exit(0)
PY
  status=$?
  if [[ $status -eq 1 ]]; then
    manifest_fail=1
  elif [[ $status -eq 2 ]]; then
    echo "[warn] Missing payload referenced by $(basename "$manifest")" >&2
  fi
done < <(find "${REPO_ROOT}/checkpoints" -name 'manifest-*.json' -print0)

if [[ $manifest_fail -eq 1 ]]; then
  echo "Digest validation failed" >&2
  exit 1
fi
