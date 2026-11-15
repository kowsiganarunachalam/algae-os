#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILDROOT_DIR="$PROJECT_DIR/buildroot"
HOST_DIR="$BUILDROOT_DIR/output/host"
TARGET="aarch64-unknown-linux-gnu"
LINKER="$HOST_DIR/bin/aarch64-buildroot-linux-gnu-gcc"

[ -x "$LINKER" ] || {
	echo "Buildroot ARM64 toolchain is missing; build Buildroot first" >&2
	exit 1
}

CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER="$LINKER" \
	cargo build --release --manifest-path "$PROJECT_DIR/system/service-manager/Cargo.toml" \
	--target "$TARGET"
install -m 0755 \
	"$PROJECT_DIR/system/service-manager/target/$TARGET/release/service-manager" \
	"$PROJECT_DIR/rootfs/overlay/usr/bin/algae-service-manager"

CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER="$LINKER" \
	cargo build --release --manifest-path "$PROJECT_DIR/system/security-demo/Cargo.toml" \
	--target "$TARGET" --bins
install -m 0755 \
	"$PROJECT_DIR/system/security-demo/target/$TARGET/release/algae-demo-app-a" \
	"$PROJECT_DIR/rootfs/overlay/usr/bin/algae-demo-app-a"
install -m 0755 \
	"$PROJECT_DIR/system/security-demo/target/$TARGET/release/algae-demo-app-b" \
	"$PROJECT_DIR/rootfs/overlay/usr/bin/algae-demo-app-b"

echo "Built service manager and ARM64 security demo applications"
