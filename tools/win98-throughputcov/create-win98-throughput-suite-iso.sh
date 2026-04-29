#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
THRU_DIR="$ROOT_DIR/tools/win98-throughputcov"
MICRO_DIR="$ROOT_DIR/tools/win98-microstress"
STAGE_DIR="$THRU_DIR/suite-iso-root"
SRC_DIR="$THRU_DIR/suite-src"
OUT_ISO="$THRU_DIR/win98-throughput-suite.iso"

if ! command -v hdiutil >/dev/null 2>&1; then
    echo "error: hdiutil not found." >&2
    exit 1
fi

"$THRU_DIR/build-win98-throughputcov.sh"
"$MICRO_DIR/build-win98-microstress.sh"

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/SCRIPTS" "$STAGE_DIR/DATA"

# Base content: keep existing validated microstress + perf scripts.
cp -R "$MICRO_DIR/unified-iso-root/"* "$STAGE_DIR/"

# Overlay throughput harness assets.
cp "$THRU_DIR/THROUGHCOV.EXE" "$STAGE_DIR/"
cp "$THRU_DIR/iso-src/SCRIPTS/"RUN_*.BAT "$STAGE_DIR/SCRIPTS/"

# Add suite docs + convenience runner.
cp "$SRC_DIR/README.TXT" "$STAGE_DIR/README.TXT"
cp "$SRC_DIR/SCRIPTS/"*.BAT "$STAGE_DIR/SCRIPTS/"

# Self-contained DATA payload for PERF_RUN.BAT (so D:\DATA exists on this same ISO).
# Uses a deterministic synthetic payload large enough to exercise copy/load paths.
for i in $(seq 1 32); do
    dd if=/dev/zero of="$STAGE_DIR/DATA/BLOB$(printf '%02d' "$i").BIN" bs=1048576 count=2 status=none
    printf 'suite-payload-file-%02d\n' "$i" > "$STAGE_DIR/DATA/NOTE$(printf '%02d' "$i").TXT"
done

# Ensure DOS/Win98-friendly CRLF endings for text/batch files.
while IFS= read -r -d '' txt; do
    awk '{ sub(/\r$/, ""); printf "%s\r\n", $0 }' "$txt" > "$txt.crlf"
    mv "$txt.crlf" "$txt"
done < <(find "$STAGE_DIR" -type f \( -iname '*.bat' -o -iname '*.txt' \) -print0)

rm -f "$OUT_ISO"
hdiutil makehybrid \
    -iso -joliet \
    -default-volume-name "WIN98SUITE" \
    -o "$OUT_ISO" \
    "$STAGE_DIR" >/dev/null

echo "Created ISO: $OUT_ISO"
echo "Mount in Win98 and run: D:\\SCRIPTS\\RUN_SUITE.BAT D:"
