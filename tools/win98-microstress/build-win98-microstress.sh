#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT_DIR/tools/win98-microstress/microstress_win98.c"
OUT="$ROOT_DIR/tools/win98-microstress/MICROSTR.EXE"

if ! command -v i686-w64-mingw32-gcc >/dev/null 2>&1; then
    echo "error: i686-w64-mingw32-gcc not found (install mingw-w64)." >&2
    exit 1
fi

i686-w64-mingw32-gcc -O2 -march=i586 -mmmx -s -Wall -Wextra \
    -o "$OUT" "$SRC"

echo "Built: $OUT"
