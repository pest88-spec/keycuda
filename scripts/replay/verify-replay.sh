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

echo "Replay verification stub" >&2
# TODO(T027A): Parse manifest and telemetry, compute digests, compare against baseline.
exit 0
