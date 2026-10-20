#!/usr/bin/env bash
# Builds the local checkout for the Waveshare RP2040-Touch-LCD-1.28 inside WSL.
#
# Usage (inside WSL, from anywhere):  bash tools/build-waveshare-wsl.sh
# Needs: git cmake ninja-build python3 build-essential gcc-arm-none-eabi
#        libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib, and Pico SDK 1.5.1
# Output: build-out/nevermore-controller_waveshare-rp2040-touch-lcd-1.28.uf2
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
SDK_DIR="${PICO_SDK_PATH:-$HOME/pico/pico-sdk}"
SRC_DIR="$HOME/pico/nevermore-local"
BUILD_DIR="$HOME/pico/nevermore-build-local"

export PICO_SDK_PATH="$SDK_DIR"
# CMake 4 rejects the SDK 1.5.1 tools' ancient cmake_minimum_required
export CMAKE_POLICY_VERSION_MINIMUM=3.5

# configure_file can't set permissions on /mnt/c, so build from a copy on the Linux FS
rm -rf "$SRC_DIR"
mkdir -p "$SRC_DIR"
(cd "$REPO" && tar --exclude=./build-out -cf - .) | (cd "$SRC_DIR" && tar -xf -)
# Windows checkout has CRLF; shebang scripts break with it
find "$SRC_DIR" -path "$SRC_DIR/.git" -prune -o \( -name '*.py' -o -name '*.sh' -o -name '*.bash' \) -type f -exec sed -i 's/\r$//' {} +

cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPICO_BOARD=waveshare_rp2040_lcd_1.28 \
  -DNEVERMORE_BOARD=waveshare-rp2040-touch-lcd-1.28 \
  -DNEVERMORE_PICO_W_BT=false
cmake --build "$BUILD_DIR" -j"$(nproc)"

mkdir -p "$REPO/build-out"
cp -v "$BUILD_DIR/nevermore-controller_waveshare-rp2040-touch-lcd-1.28.uf2" "$REPO/build-out/"
