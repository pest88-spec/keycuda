#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DIGEST_BIN="${REPO_ROOT}/bin/puzzle71_digest"

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

# TODO: hook into real digest verifier binary once implemented.
if [[ -x "$DIGEST_BIN" ]]; then
  "$DIGEST_BIN" --manifest "$MANIFEST" --telemetry "$TELEMETRY"
else
  echo "Replay verification stub — digest tool missing" >&2
fi
