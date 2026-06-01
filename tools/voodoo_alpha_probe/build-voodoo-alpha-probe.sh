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
