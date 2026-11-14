#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
IMAGE_DIR="$PROJECT_DIR/buildroot/output/images"

exec "$PROJECT_DIR/buildroot/output/host/bin/qemu-system-aarch64" \
    -M virt -cpu cortex-a53 -m 2048 -smp 2 \
    -kernel "$IMAGE_DIR/Image" \
    -append 'rootwait root=/dev/vda console=ttyAMA0' \
    -display vnc=127.0.0.1:0 -serial mon:stdio \
    -device virtio-gpu-pci -device virtio-keyboard-pci \
    -device virtio-tablet-pci \
    -netdev user,id=eth0 -device virtio-net-device,netdev=eth0 \
    -drive "file=$IMAGE_DIR/rootfs.ext4,if=none,format=raw,id=hd0" \
    -device virtio-blk-device,drive=hd0 "$@"
