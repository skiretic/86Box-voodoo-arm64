#!/bin/bash
#
# 86Box macOS Rosetta 2 x86_64 — Setup & Build Script
#
# Usage:
#   ./scripts/setup-and-build-rosetta-x86_64.sh deps    Install required x86_64 Homebrew dependencies
#   ./scripts/setup-and-build-rosetta-x86_64.sh build   Clean configure + build + codesign .app (x86_64)
#   ./scripts/setup-and-build-rosetta-x86_64.sh         Show help
#

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

run_rosetta() {
    arch -x86_64 /bin/zsh -lc "$*"
}

# ---------------------------------------------------------------------------
# deps — install x86_64 Homebrew packages via Rosetta
# ---------------------------------------------------------------------------
cmd_deps() {
    if [[ "$(uname -s)" != "Darwin" ]]; then
        error "This script targets macOS only."
    fi

    info "Checking Rosetta 2 availability..."
    if ! arch -x86_64 /usr/bin/true >/dev/null 2>&1; then
        error "Rosetta 2 is not available. Install it with: softwareupdate --install-rosetta"
    fi

    info "Checking for x86_64 Homebrew in /usr/local/bin..."
    if [[ ! -x /usr/local/bin/brew ]]; then
        error "x86_64 Homebrew not found at /usr/local/bin/brew. Install Intel Homebrew under Rosetta first."
    fi

    info "Installing required x86_64 dependencies via Rosetta Homebrew..."
    run_rosetta 'export PATH=/usr/local/bin:$PATH; brew install cmake ninja sdl2 rtmidi openal-soft fluidsynth libslirp vde libserialport qt@5'

    info "Dependencies installed."
    echo ""
    info "Next step:  ./scripts/setup-and-build-rosetta-x86_64.sh build"
}

# ---------------------------------------------------------------------------
# build — configure x86_64, compile, codesign
# ---------------------------------------------------------------------------
cmd_build() {
    cd "$REPO_ROOT"

    if [[ "$(uname -s)" != "Darwin" ]]; then
        error "This script targets macOS only."
    fi

    info "Checking Rosetta 2 availability..."
    if ! arch -x86_64 /usr/bin/true >/dev/null 2>&1; then
        error "Rosetta 2 is not available. Install it with: softwareupdate --install-rosetta"
    fi

    NCPU="$(sysctl -n hw.ncpu)"
    BUILD_DIR="build-x86_64"

    # Resolve x86_64 Homebrew prefixes under Rosetta
    QT5_ROOT="$(run_rosetta 'export PATH=/usr/local/bin:$PATH; brew --prefix qt@5')"
    OPENAL_ROOT="$(run_rosetta 'export PATH=/usr/local/bin:$PATH; brew --prefix openal-soft')"
    LIBSERIALPORT_ROOT="$(run_rosetta 'export PATH=/usr/local/bin:$PATH; brew --prefix libserialport')"

    for pkg in "$QT5_ROOT" "$OPENAL_ROOT" "$LIBSERIALPORT_ROOT"; do
        [[ -d "$pkg" ]] || error "Missing dependency at $pkg — run: ./scripts/setup-and-build-rosetta-x86_64.sh deps"
    done

    if [[ -d "$BUILD_DIR" ]]; then
        info "Removing old build directory..."
        rm -rf "$BUILD_DIR"
    fi

    info "Configuring x86_64 build (CMake + Rosetta)..."
    run_rosetta "export PATH=/usr/local/bin:\$PATH; cmake -S '$REPO_ROOT' -B '$REPO_ROOT/$BUILD_DIR' -G Ninja \
        -D CMAKE_BUILD_TYPE=Release \
        -D CMAKE_OSX_ARCHITECTURES=x86_64 \
        -D NEW_DYNAREC=ON \
        -D QT=ON \
        -D USE_QT6=OFF \
        -D Qt5_DIR='$QT5_ROOT/lib/cmake/Qt5' \
        -D Qt5LinguistTools_DIR='$QT5_ROOT/lib/cmake/Qt5LinguistTools' \
        -D OpenAL_ROOT='$OPENAL_ROOT' \
        -D LIBSERIALPORT_ROOT='$LIBSERIALPORT_ROOT'"

    info "Building with $NCPU parallel jobs (Rosetta)..."
    run_rosetta "export PATH=/usr/local/bin:\$PATH; cmake --build '$REPO_ROOT/$BUILD_DIR' -j'$NCPU'"

    info "Codesigning 86Box.app (ad-hoc with JIT entitlement)..."
    codesign -s - \
        --entitlements src/mac/entitlements.plist \
        --force \
        "$BUILD_DIR/src/86Box.app"

    info "Verifying architecture..."
    file "$BUILD_DIR/src/86Box.app/Contents/MacOS/86Box"

    echo ""
    info "Build complete! App is at:"
    echo "  $REPO_ROOT/$BUILD_DIR/src/86Box.app"
    echo ""
    info "To run under Rosetta:"
    echo "  arch -x86_64 $BUILD_DIR/src/86Box.app/Contents/MacOS/86Box -P"
}

# ---------------------------------------------------------------------------
# help
# ---------------------------------------------------------------------------
cmd_help() {
    echo "86Box macOS Rosetta 2 x86_64 Build Script"
    echo ""
    echo "Usage:"
    echo "  ./scripts/setup-and-build-rosetta-x86_64.sh deps    Install x86_64 Homebrew dependencies"
    echo "  ./scripts/setup-and-build-rosetta-x86_64.sh build   Clean x86_64 build + codesign .app"
    echo ""
    echo "Requirements:"
    echo "  - macOS"
    echo "  - Rosetta 2 (softwareupdate --install-rosetta)"
    echo "  - Intel/x86_64 Homebrew in /usr/local"
    echo "  - Xcode Command Line Tools (xcode-select --install)"
}

case "${1:-help}" in
    deps)  cmd_deps  ;;
    build) cmd_build ;;
    *)     cmd_help  ;;
esac
