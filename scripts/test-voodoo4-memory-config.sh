#!/bin/bash

set -euo pipefail

FILE="src/video/vid_voodoo_banshee.c"

if ! rg -q '\.name\s*=\s*"memory"' "$FILE"; then
    echo "FAIL: memory config block not found in $FILE" >&2
    exit 1
fi

if ! rg -q '\{ \.description = "32 MB", \.value = 32 \}' "$FILE"; then
    echo "FAIL: Voodoo4/Banshee memory config does not expose a 32 MB option" >&2
    exit 1
fi

if ! rg -q 'mem_size = 32; /\* Voodoo4 4500 uses 32 MB SDRAM \*/' "$FILE"; then
    echo "FAIL: Voodoo4 init path does not default SDRAM cards to 32 MB" >&2
    exit 1
fi

if ! rg -Fq 'banshee->dramInit0 = 0x18000000; /* 4 chips * 8 MB/chip = 32 MB per Linux tdfxfb Voodoo4/5 sizing logic */' "$FILE"; then
    echo "FAIL: Voodoo4 dramInit0 does not report a 32 MB Voodoo4/5 framebuffer layout" >&2
    exit 1
fi

if ! rg -q 'ret = 0x00000448; /\* SDRAM, AGP, 16 Mbit strap pin, IRQ enabled, 32kB BIOS \*/' "$FILE"; then
    echo "FAIL: Voodoo4 strapInfo does not report a V4-specific SDRAM/AGP strap value" >&2
    exit 1
fi

echo "PASS: Voodoo4 memory config exposes and defaults to 32 MB"
