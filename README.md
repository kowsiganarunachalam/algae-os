
# RustOS

A privacy-focused, Linux-based mobile operating system.

## Technology
- Linux Kernel
- Rust
- ARM64
- QEMU
- Buildroot
- Slint

## Project Status
QEMU ARM64 boots a Weston desktop with an Algae home screen and Rust service control.

## QEMU ARM64 graphical image

The `algae_qemu_defconfig` enables the QEMU virtio GPU kernel driver, eudev,
an early framebuffer splash, and the Weston DRM compositor. Build and launch it in order:

```sh
make -C buildroot BR2_DEFCONFIG="$PWD/algae_qemu_defconfig" defconfig
./scripts/build-algae-home.sh
make -C buildroot -j2
./scripts/run-qemu-graphical.sh
```

In a second terminal, connect to QEMU's local display with
`vncviewer 127.0.0.1:0`. The buildroot QEMU binary provides VNC and virtio GPU;
the launcher keeps its serial console attached to the first terminal. Weston
shows an animated algae-shaped loading mark for about 5 seconds, then opens
the Algae home screen. If display 0 is occupied, launch with
`VNC_DISPLAY=1 ./scripts/run-qemu-graphical.sh` and connect to `127.0.0.1:1`.
The Services tile explains service
management; at the serial console, control services with `algae-service status`,
`algae-service stop demo-service`, `algae-service start demo-service`, and
`algae-service restart demo-service`.

BusyBox init remains PID 1; the Algae service manager runs as a separate
background process.

### Boot style

Choose **Settings** on the Algae home screen, then select Logo, Kelp, Pixel,
or Sprig under “Boot style.” The choice is saved in `/etc/algae/boot-style`
and used by the early framebuffer animation and the loading animation after
Weston starts. The Logo style follows the Algae mark in the branding reference.
