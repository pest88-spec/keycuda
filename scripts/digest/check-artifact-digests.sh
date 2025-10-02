#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DIGEST_DIR="${REPO_ROOT}/digests"
OUTPUT="${DIGEST_DIR}/latest.json"

python3 - "$REPO_ROOT" "$OUTPUT" "${REPO_ROOT}/checkpoints" "${REPO_ROOT}/telemetry" "${REPO_ROOT}/benchmarks" "${REPO_ROOT}/reports" <<'PY'
import hashlib
import json
import sys
from pathlib import Path

repo = Path(sys.argv[1])
output = Path(sys.argv[2])
artifact_dirs = [Path(p) for p in sys.argv[3:]]

output.parent.mkdir(parents=True, exist_ok=True)

digests = {}
for directory in artifact_dirs:
    if not directory.exists():
        continue
    for file_path in sorted(directory.rglob("*")):
        if file_path.is_file():
            rel = file_path.relative_to(repo)
            digests[str(rel)] = hashlib.sha256(file_path.read_bytes()).hexdigest()

output.write_text(json.dumps(digests, indent=2) + "\n")
print(f"[info] Digest manifest written to {output}")

manifest_results = []
status = 0
missing_payload = False
checkpoint_root = repo / "checkpoints"
if checkpoint_root.exists():
    manifests = sorted(checkpoint_root.rglob("manifest-*.json"))
else:
    manifests = []

for manifest in manifests:
    try:
        data = json.loads(manifest.read_text())
    except json.JSONDecodeError as exc:
        manifest_results.append({
            "status": "parse_error",
            "manifest": str(manifest),
            "error": str(exc)
        })
        status = 1
        continue

    payload_value = data.get("path", "")
    expected = data.get("payload_sha256")
    payload_path = Path(payload_value)
    if not payload_path.is_absolute():
        payload_path = manifest.parent / payload_path

    if not payload_path.exists():
        manifest_results.append({
            "status": "missing_payload",
            "manifest": str(manifest)
        })
        missing_payload = True
        continue

    actual = hashlib.sha256(payload_path.read_bytes()).hexdigest()
    if expected and actual != expected:
        manifest_results.append({
            "status": "mismatch",
            "manifest": str(manifest),
            "expected": expected,
            "actual": actual
        })
        status = 1
    else:
        manifest_results.append({
            "status": "ok",
            "manifest": str(manifest)
        })

manifest_output = output.parent / "manifest_results.json"
manifest_output.write_text(json.dumps(manifest_results, indent=2) + "\n")

if status != 0:
    print("[error] Digest validation failed", file=sys.stderr)
    sys.exit(1)

if missing_payload:
    print("[warn] One or more manifests reference missing payloads", file=sys.stderr)

sys.exit(0)
PY
