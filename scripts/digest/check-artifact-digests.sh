#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

DIGEST_DIR="${REPO_ROOT}/digests"
ARTIFACT_LIST=("checkpoints" "telemetry" "benchmarks" "reports")

mkdir -p "$DIGEST_DIR"

for artifact in "${ARTIFACT_LIST[@]}"; do
  echo "Checking digests for $artifact (stub)" >&2
  # TODO(T045): Iterate artifacts, compute SHA-256, compare against manifests, and record output.

done

exit 0
