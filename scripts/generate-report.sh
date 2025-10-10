#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUTPUT="${REPO_ROOT}/reports/puzzle71-run-$(date +%Y%m%d).md"

echo "Generating report -> ${OUTPUT}" >&2

TIMESTAMP=$(date --iso-8601=seconds)
DIGEST_JSON="${REPO_ROOT}/digests/latest.json"
BENCH_JSON="${REPO_ROOT}/benchmarks/latest.json"

mkdir -p "${REPO_ROOT}/reports"

{
  echo "# Puzzle71Solver Run Report"
  echo "*Generated:* ${TIMESTAMP}"
  echo
  echo "## Artifacts"
  if [[ -f "$DIGEST_JSON" ]]; then
    echo "- Digests: ${DIGEST_JSON#$REPO_ROOT/}"
  else
    echo "- Digests: (missing)"
  fi
  if [[ -f "$BENCH_JSON" ]]; then
    echo "- Benchmarks: ${BENCH_JSON#$REPO_ROOT/}"
  else
    echo "- Benchmarks: (missing)"
  fi
  echo
  echo "## Checkpoint Manifests"
  python3 - <<'PY'
import json
from pathlib import Path
import hashlib

root = Path("${REPO_ROOT}")
manifests = sorted(root.glob("checkpoints/manifest-*.json"))
if not manifests:
    print("(no manifests found)")
else:
    for manifest in manifests:
        data = json.loads(manifest.read_text())
        payload_path = data.get("path")
        expected = data.get("payload_sha256")
        status = "missing"
        path_display = payload_path
        if payload_path:
            payload = Path(payload_path)
            if payload.exists():
                sha = hashlib.sha256(payload.read_bytes()).hexdigest()
                status = "ok" if expected == sha else f"sha mismatch (expected {expected}, got {sha})"
        print(f"- {manifest.name}: {status}")
PY
  echo
  echo "## QA Commands"
  echo '```'
} > "$OUTPUT"

{
  echo "scripts/run-benchmarks.sh 0 1"
  echo "scripts/digest/check-artifact-digests.sh"
  echo "scripts/run-qa.sh"
  echo '```'
} >> "$OUTPUT"

echo "Report written to ${OUTPUT}" >&2
exit 0
