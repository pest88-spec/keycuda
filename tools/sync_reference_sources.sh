#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_FILE="${ROOT_DIR}/docs/reference-locks.md"
declare -a SOURCES=(
  "VanitySearch|https://github.com/JeanLucPons/VanitySearch.git"
  "BitCrack|https://github.com/brichard19/BitCrack.git"
  "CudaBrainSecp|https://github.com/XopMC/CudaBrainSecp.git"
  "secp256k1-zkp|https://github.com/ElementsProject/secp256k1-zkp.git"
  "bitcoin-core-secp256k1|https://github.com/bitcoin-core/secp256k1.git"
)

if [[ "${1:-}" != "--apply" ]]; then
  echo "Usage: $(basename "$0") --apply" >&2
  exit 1
fi

tmp_file="$(mktemp)"
{
  echo "# Reference Locks"
  echo
  echo "Last synced: $(date --iso-8601=seconds)"
  echo
  echo "| Source | Repository | Commit |"
  echo "|--------|------------|--------|"
  for entry in "${SOURCES[@]}"; do
    name="${entry%%|*}"
    repo="${entry##*|}"
    commit="$(git ls-remote "$repo" HEAD | awk '{print $1}')"
    if [[ -z "$commit" ]]; then
      echo "Error: Unable to fetch commit for $name ($repo)" >&2
      exit 2
    fi
    echo "| ${name} | ${repo} | ${commit} |"
  done
} >"$tmp_file"

mv "$tmp_file" "$OUTPUT_FILE"
chmod 644 "$OUTPUT_FILE"

echo "Reference locks written to $OUTPUT_FILE"