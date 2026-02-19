#!/bin/bash
#
# Build a fully-bundled 86Box AppImage for Linux ARM64 (aarch64)
#
# This script is designed to run inside the Docker container.
# From the host: ./appimage/build.sh
#
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

SRC_DIR="/src"
BUILD_DIR="/build/cmake-build"
APPDIR="/build/AppDir"
OUTPUT_DIR="/output"

# Verify source is mounted
[[ -f "$SRC_DIR/CMakeLists.txt" ]] || error "Source not mounted at $SRC_DIR"

# -----------------------------------------------------------------------
# Step 1: Build 86Box
# -----------------------------------------------------------------------
info "Configuring 86Box..."
cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja \
    -D CMAKE_TOOLCHAIN_FILE="$SRC_DIR/cmake/flags-gcc-aarch64.cmake" \
    -D CMAKE_BUILD_TYPE=Release \
    -D NEW_DYNAREC=ON \
    -D QT=ON \
    -D CMAKE_INSTALL_PREFIX=/usr/local \
    -D CMAKE_EXPORT_COMPILE_COMMANDS=ON

info "Building 86Box..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

# -----------------------------------------------------------------------
# Step 2: Prepare AppDir structure
# -----------------------------------------------------------------------
info "Preparing AppDir..."
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/local/bin"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"
mkdir -p "$APPDIR/usr/share/metainfo"

# Install binary
cp "$BUILD_DIR/src/86Box" "$APPDIR/usr/local/bin/"
strip "$APPDIR/usr/local/bin/86Box"

# Install mdsx plugin if built
if [[ -f "$BUILD_DIR/src/mdsx.so" ]]; then
    cp "$BUILD_DIR/src/mdsx.so" "$APPDIR/usr/lib/"
    info "Included mdsx.so MIDI plugin"
fi

# Desktop file
cat > "$APPDIR/usr/share/applications/net.86box.86Box.desktop" << 'DESKTOP'
[Desktop Entry]
Type=Application
Name=86Box
Comment=IBM PC system emulator
Exec=86Box
Icon=net.86box.86Box
Categories=System;Emulator;
Terminal=false
StartupNotify=true
DESKTOP

# Icon — use the one from the source tree if available
if [[ -f "$SRC_DIR/src/unix/assets/256x256/net.86box.86Box.png" ]]; then
    cp "$SRC_DIR/src/unix/assets/256x256/net.86box.86Box.png" \
       "$APPDIR/usr/share/icons/hicolor/256x256/apps/"
elif [[ -f "$SRC_DIR/src/unix/assets/net.86box.86Box.png" ]]; then
    cp "$SRC_DIR/src/unix/assets/net.86box.86Box.png" \
       "$APPDIR/usr/share/icons/hicolor/256x256/apps/"
else
    warn "No icon found — AppImage will work but have no icon"
fi

# NOTE: Do NOT create top-level symlinks (.DirIcon, .desktop, icon.png)
# appimage-builder handles those automatically and will error on conflicts.

# README with build info
cat > "$APPDIR/README" << README
Libraries used to compile this arm64 build of 86Box:
$(dpkg -l | grep '^ii' | grep -E 'lib(sdl2|openal|rtmidi|fluidsynth|slirp|vdeplug|serialport|sndfile|sndio|evdev|freetype|png|glib|x11|xkbcommon|qt5|wayland|instpatch|samplerate|stdc\+\+|c6 )' | awk '{printf "%16s %s\n", $2, $3}')
             qt5 $(dpkg -l | grep libqt5core5a | awk '{print $3}')
README

# -----------------------------------------------------------------------
# Step 3: Run appimage-builder
# -----------------------------------------------------------------------
info "Running appimage-builder..."
cd /build
cp "$SRC_DIR/appimage/AppImageBuilder.yml" /build/

appimage-builder --recipe AppImageBuilder.yml --skip-tests

# -----------------------------------------------------------------------
# Step 4: Copy output
# -----------------------------------------------------------------------
info "Copying AppImage to output..."
mkdir -p "$OUTPUT_DIR"
cp /build/*.AppImage "$OUTPUT_DIR/" 2>/dev/null || true
cp /build/*.AppImage.zsync "$OUTPUT_DIR/" 2>/dev/null || true

APPIMAGE=$(ls "$OUTPUT_DIR"/*.AppImage 2>/dev/null | head -1)
if [[ -n "$APPIMAGE" ]]; then
    SIZE=$(du -h "$APPIMAGE" | cut -f1)
    info "AppImage built: $(basename "$APPIMAGE") ($SIZE)"
    info "Output at: $OUTPUT_DIR/"
else
    error "No AppImage produced — check build output above"
fi
