#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

i686-w64-mingw32-gcc \
  -Os \
  -ffreestanding \
  -fno-builtin \
  -march=i586 \
  -mwindows \
  -nostdlib \
  -Wl,-e,_WinMainCRTStartup \
  -Wl,--subsystem,windows \
  voodoo_alpha_probe.c \
  -lkernel32 \
  -luser32 \
  -o ALPHAPRB.EXE

file ALPHAPRB.EXE

iso_tool=
if command -v xorrisofs >/dev/null 2>&1; then
  iso_tool=xorrisofs
elif command -v genisoimage >/dev/null 2>&1; then
  iso_tool=genisoimage
elif command -v mkisofs >/dev/null 2>&1; then
  iso_tool=mkisofs
fi

if [[ -n "$iso_tool" ]]; then
  "$iso_tool" -quiet -o alphaprb.iso -V ALPHAPRB ALPHAPRB.EXE
  file alphaprb.iso
else
  echo "ISO not built: xorrisofs/genisoimage/mkisofs not found"
fi
