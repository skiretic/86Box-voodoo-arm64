#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
KIT_DIR="$ROOT_DIR/tools/win98-microstress"
SRC_DIR="$KIT_DIR/iso-src"
STAGE_DIR="$KIT_DIR/iso-root"
OUT_ISO="$KIT_DIR/win98-microstress-kit.iso"

if ! command -v hdiutil >/dev/null 2>&1; then
    echo "error: hdiutil not found." >&2
    exit 1
fi

# Always rebuild EXE first so ISO content matches current source/flags.
"$KIT_DIR/build-win98-microstress.sh"

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/SCRIPTS"

cp "$KIT_DIR/MICROSTR.EXE" "$STAGE_DIR/"
cp "$SRC_DIR/README.TXT" "$STAGE_DIR/README.TXT"
cp "$SRC_DIR/SCRIPTS/"*.BAT "$STAGE_DIR/SCRIPTS/"

# Win98/DOS command parser is most reliable with CRLF endings for BAT/TXT assets.
awk '{ sub(/\r$/, ""); printf "%s\r\n", $0 }' "$STAGE_DIR/README.TXT" > "$STAGE_DIR/README.TXT.crlf"
mv "$STAGE_DIR/README.TXT.crlf" "$STAGE_DIR/README.TXT"
for bat in "$STAGE_DIR"/SCRIPTS/*.BAT; do
    awk '{ sub(/\r$/, ""); printf "%s\r\n", $0 }' "$bat" > "$bat.crlf"
    mv "$bat.crlf" "$bat"
done

rm -f "$OUT_ISO"
# Joliet keeps long names readable from Win98 Explorer while preserving DOS compatibility.
hdiutil makehybrid \
    -iso -joliet \
    -default-volume-name "WIN98MICRO" \
    -o "$OUT_ISO" \
    "$STAGE_DIR" >/dev/null

echo "Created ISO: $OUT_ISO"
echo "Mount in Win98 and run: D:\\SCRIPTS\\MICRO_RUN.BAT D:"
