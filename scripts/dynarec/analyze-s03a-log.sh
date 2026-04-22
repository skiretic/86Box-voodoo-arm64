#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${ROOT_DIR}/scripts/dynarec/.analyze-s03a-log.bin"
SRC="${ROOT_DIR}/scripts/dynarec/analyze-s03a-log.c"

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  echo "usage: $0 <current-log> [baseline-log]" >&2
  exit 2
fi

cc -O2 -std=c11 -Wall -Wextra -Werror -o "${BIN}" "${SRC}"
"${BIN}" "$@"
