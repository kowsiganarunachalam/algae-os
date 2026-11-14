#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILDROOT_DIR="$PROJECT_DIR/buildroot"
STAGING_DIR="$BUILDROOT_DIR/output/staging"
HOST_DIR="$BUILDROOT_DIR/output/host"
SOURCE="$PROJECT_DIR/system/algae-home/main.c"
OUTPUT="$PROJECT_DIR/rootfs/overlay/usr/bin/algae-home"
PROTOCOL="$STAGING_DIR/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml"
TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT HUP INT TERM

if [ ! -x "$HOST_DIR/bin/aarch64-buildroot-linux-gnu-gcc" ] || [ ! -f "$PROTOCOL" ]; then
    echo "Buildroot ARM64 toolchain and Wayland protocols must be built first" >&2
    exit 1
fi

"$HOST_DIR/bin/wayland-scanner" client-header "$PROTOCOL" \
    "$TMP_DIR/xdg-shell-client-protocol.h"
"$HOST_DIR/bin/wayland-scanner" private-code "$PROTOCOL" \
    "$TMP_DIR/xdg-shell-protocol.c"

PKG_CONFIG_PATH="$STAGING_DIR/usr/lib/pkgconfig" \
PKG_CONFIG_LIBDIR="$STAGING_DIR/usr/lib/pkgconfig" \
PKG_CONFIG_SYSROOT_DIR="$STAGING_DIR" \
    "$HOST_DIR/bin/aarch64-buildroot-linux-gnu-gcc" -O2 -Wall -Wextra \
    -I"$TMP_DIR" \
    $(PKG_CONFIG_PATH="$STAGING_DIR/usr/lib/pkgconfig" \
      PKG_CONFIG_LIBDIR="$STAGING_DIR/usr/lib/pkgconfig" \
      PKG_CONFIG_SYSROOT_DIR="$STAGING_DIR" \
      pkg-config --cflags wayland-client cairo) \
    "$SOURCE" "$TMP_DIR/xdg-shell-protocol.c" \
    $(PKG_CONFIG_PATH="$STAGING_DIR/usr/lib/pkgconfig" \
      PKG_CONFIG_LIBDIR="$STAGING_DIR/usr/lib/pkgconfig" \
      PKG_CONFIG_SYSROOT_DIR="$STAGING_DIR" \
      pkg-config --libs wayland-client cairo) \
    -lm \
    -o "$OUTPUT"

chmod 0755 "$OUTPUT"
echo "Built $OUTPUT"
