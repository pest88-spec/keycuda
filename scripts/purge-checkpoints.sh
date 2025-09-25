#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

RETENTION=${1:-"30d"}

echo "Checkpoint purge stub (retention=${RETENTION})" >&2
# TODO(T047): Enumerate checkpoints, verify digests, rotate passphrases, delete expired files.
exit 0
