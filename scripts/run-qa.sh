#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

MODE="full"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --mode)
      MODE="${2:-full}"
      shift 2
      ;;
    --smoke)
      MODE="smoke"
      shift
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 2
      ;;
  esac
done

# Configure & build if needed
if [[ ! -d "${REPO_ROOT}/build" ]]; then
  cmake -S "${REPO_ROOT}" -B "${REPO_ROOT}/build" -DCMAKE_BUILD_TYPE=RelWithDebInfo
fi

cmake --build "${REPO_ROOT}/build" --config RelWithDebInfo >/dev/null

pushd "${REPO_ROOT}/build" >/dev/null
if [[ "$MODE" == "smoke" ]]; then
  ctest --output-on-failure -E "tests/perf" || exit 1
else
  ctest --output-on-failure || exit 1
fi
popd >/dev/null

if [[ "$MODE" == "smoke" ]]; then
  echo "[qa] Smoke mode: skipping benchmark sweep" >&2
else
  if [[ -n "${PUZZLE71_BENCHMARK_ARGS:-}" ]]; then
    echo "[qa] Using custom benchmark args: ${PUZZLE71_BENCHMARK_ARGS}" >&2
    # shellcheck disable=SC2086
    "${SCRIPT_DIR}/run-benchmarks.sh" ${PUZZLE71_BENCHMARK_ARGS}
  else
    "${SCRIPT_DIR}/run-benchmarks.sh" --devices 0 --samples 1
  fi
fi

"${SCRIPT_DIR}/digest/check-artifact-digests.sh"

manifest_glob="${REPO_ROOT}/checkpoints"
if [[ -d "$manifest_glob" ]]; then
  # Smoke mode checks仅抽取最新清单以避免过久循环
  if [[ "$MODE" == "smoke" ]]; then
    manifests=$(find "$manifest_glob" -name 'manifest-*.json' -print | sort | tail -n 1)
  else
    manifests=$(find "$manifest_glob" -name 'manifest-*.json' -print)
  fi
  telemetry="${REPO_ROOT}/telemetry/replay.jsonl"
  if [[ -n "$manifests" && -f "$telemetry" ]]; then
    for manifest in $manifests; do
      "${SCRIPT_DIR}/replay/verify-replay.sh" "$manifest" "$telemetry"
    done
  fi
fi

echo "QA pipeline complete (${MODE})" >&2
