#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Configure & build if needed
if [[ ! -d "${REPO_ROOT}/build" ]]; then
  cmake -S "${REPO_ROOT}" -B "${REPO_ROOT}/build" -DCMAKE_BUILD_TYPE=RelWithDebInfo
fi

cmake --build "${REPO_ROOT}/build" --config RelWithDebInfo >/dev/null

pushd "${REPO_ROOT}/build" >/dev/null
ctest --output-on-failure || exit 1
popd >/dev/null

"${SCRIPT_DIR}/run-benchmarks.sh" "0" "1"
"${SCRIPT_DIR}/digest/check-artifact-digests.sh"

manifests=$(find "${REPO_ROOT}/checkpoints" -name 'manifest-*.json' -print -quit)
if [[ -n "$manifests" ]]; then
  for manifest in $manifests; do
    telemetry="${REPO_ROOT}/telemetry/replay.jsonl"
    if [[ -f "$telemetry" ]]; then
      "${SCRIPT_DIR}/replay/verify-replay.sh" "$manifest" "$telemetry"
    fi
  done
fi

echo "QA pipeline complete" >&2
