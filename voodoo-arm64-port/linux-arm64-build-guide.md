# Building 86Box for ARM64 Linux (AppImage)

Guide for building a portable 86Box AppImage for ARM64 Linux using Docker on an Apple Silicon Mac.

## Prerequisites

- Docker Desktop installed on macOS (Apple Silicon runs ARM64 containers natively)

## 1. Create a Privileged Docker Container

The `--privileged` flag is required for FUSE, which the AppImage tools need.

```bash
docker create -it --privileged --name 86box-linux-arm64 debian:bookworm /bin/bash
docker start -ai 86box-linux-arm64
```

## 2. Install Build Dependencies

Inside the container:

```bash
apt update && apt install -y \
  build-essential cmake ninja-build git pkg-config \
  libsdl2-dev libopenal-dev librtmidi-dev \
  libfluidsynth-dev libslirp-dev libserialport-dev \
  qtbase5-dev qttools5-dev qtbase5-private-dev \
  libevdev-dev libfreetype-dev libx11-dev libxrandr-dev \
  file fuse libfuse2 wget imagemagick
```

## 3. Clone and Build

```bash
git clone https://github.com/skiretic/86Box-voodoo-arm64.git /src
cd /src

cmake --preset flags-gcc-aarch64-regular
cmake --build out/build/flags-gcc-aarch64-regular
```

## 4. Create the AppImage

```bash
cd /src

# Download AppImage tools
wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-aarch64.AppImage
wget https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-aarch64.AppImage
chmod +x linuxdeploy-aarch64.AppImage linuxdeploy-plugin-qt-aarch64.AppImage

# Create desktop file
echo '[Desktop Entry]
Name=86Box
Exec=86Box
Icon=86Box
Type=Application
Categories=System;Emulator;' > 86Box.desktop

# Create a valid icon (must be 512x512 or smaller)
convert src/qt/assets/86Box-green.png -resize 256x256 86Box.png

# Build the AppImage
export QMAKE=/usr/bin/qmake
./linuxdeploy-aarch64.AppImage \
  --appdir AppDir \
  --executable out/build/flags-gcc-aarch64-regular/src/86Box \
  --plugin qt \
  --desktop-file 86Box.desktop \
  --icon-file 86Box.png \
  --output appimage
```

## 5. Extract the AppImage

From your Mac terminal (not the container):

```bash
docker cp 86box-linux-arm64:/src/86Box-aarch64.AppImage .
```

## Container Management

| Action | Command |
|--------|---------|
| Enter container | `docker start -ai 86box-linux-arm64` |
| Exit (keeps state) | `exit` or `Ctrl-D` |
| Check it exists | `docker ps -a --filter name=86box-linux-arm64` |
| Delete it | `docker rm -f 86box-linux-arm64` |

The container is persistent — installed packages and build artifacts survive across restarts.

## Rebuilding After Code Changes

To rebuild after pulling new changes:

```bash
docker start -ai 86box-linux-arm64
cd /src
git pull
cmake --build out/build/flags-gcc-aarch64-regular
```

Then re-run the AppImage packaging step from step 4 (remove the old `AppDir` first with `rm -rf AppDir`).
