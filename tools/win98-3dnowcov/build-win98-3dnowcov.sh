#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT_DIR/tools/win98-3dnowcov/3dnowcov_win98.c"
OUT="$ROOT_DIR/tools/win98-3dnowcov/3DNOWCOV.EXE"

if ! command -v i686-w64-mingw32-gcc >/dev/null 2>&1; then
    echo "error: i686-w64-mingw32-gcc not found (install mingw-w64)." >&2
    exit 1
fi

# CRT-free build for Win98 compatibility and deterministic bare-kernel32 startup.
i686-w64-mingw32-gcc -O2 -march=i586 -mmmx -m3dnow -s -Wall -Wextra \
    -fno-stack-protector -ffreestanding -nostdlib \
    -Wl,-e,_start -Wl,--subsystem,console \
    -o "$OUT" "$SRC" -lkernel32

echo "Built: $OUT"

