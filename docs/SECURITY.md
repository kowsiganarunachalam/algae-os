# Algae OS security foundation

## Phase 1 implementation report

Algae OS uses Buildroot, Linux 6.18.7 on ARM64, a Rust service manager, and a
writable ext4 QEMU root filesystem. Weston DRM, the Wayland home app, virtio GPU,
keyboard, tablet, block device, and QEMU user-mode network are present. QEMU
verified the boot animation, Weston, home tiles, and the Services tile click.

The host has two configured build jobs, about 17 GiB free disk, and about
2.4 GiB available memory at inspection time. Existing Buildroot downloads,
toolchain, sources, and object files were retained; no clean or distclean was
run. Buildroot builds were incremental.

## Kernel and Buildroot

The Buildroot defconfig selects Linux 6.18.7, the ARM64 board kernel config,
an ext4 root image, the Algae overlay/users table, audit, libcap, libseccomp,
and nftables.

The resolved kernel config enables seccomp/filter, namespaces (user, PID,
network, IPC, UTS), cgroups (memory, PIDs, CPU accounting/scheduling), KASLR,
strong stack protection, strict kernel/module RWX, FORTIFY_SOURCE, audit,
Landlock, Yama, and Lockdown. The LSM list is `landlock,lockdown,yama`.
QEMU logs confirmed Landlock and Yama initialized, and virtio GPU/input drivers
were present. QEMU Cortex-A53 does not provide MTE or pointer authentication.
`CONFIG_BPF_SYSCALL` is disabled, so no unprivileged-BPF sysctl was added.

## Runtime settings and filesystem

`99-algae-security.conf` sets restricted dmesg and pointer access, ptrace scope
2, and protected links/files. QEMU reported successful sysctl startup and
observed `dmesg_restrict=1`, `kptr_restrict=2`, and `yama.ptrace_scope=2`.

The nftables policy defaults input and output to drop, allows loopback and
established traffic, and permits the root-owned DHCP/DNS operations needed by
QEMU. DHCP and DNS work in QEMU. The output chain has an initially empty UID
set for app outbound grants. An app requesting outbound access is added to the
set for its process lifetime, then removed on normal exit. QEMU verified App B
DNS succeeds with the grant and that the set is empty after exit. App-specific
inbound grants are not implemented; input remains default-deny except for
established replies and essential system protocols.

The root filesystem remains writable. `/run` and `/tmp` are temporary
filesystems; `/var` and system files still use the root filesystem. Read-only
root operation is deferred until writable state and startup dependencies are
fully accounted for.

## Service identities and application policy

The Rust service manager applies empty supplementary groups, drops the
capability bounding set for non-root services, sets the configured GID/UID,
and sets `no_new_privs` before exec. It rejects malformed or duplicate service
entries and aborts launches on identity setup failure. QEMU confirmed the demo
and clock UIDs/GIDs, empty capability bounding sets, and `NoNewPrivs=1`.
Weston remains root to preserve the current DRM/seat setup.

The manager's `--run-app NAME -- PROGRAM [ARG ...]` path loads
`/etc/algae/permissions/NAME.conf`, verifies the policy file is root-owned and
not group/world-writable, resolves the unprivileged app identity, then drops
supplementary groups/capabilities and applies UID/GID and `no_new_privs` before
exec. It enforces Landlock path rules and a seccomp filter before the target
program starts. Unknown keys, duplicate singleton keys, unsafe paths, symlink
paths, and unsupported policy requests are rejected. Applications get their
declared `/usr/share/NAME` read access and `/var/lib/algae/apps/NAME` write
access, plus read/execute access to `/usr/bin`, `/usr/lib`, and `/lib` for
runtime loading.

With network disabled, the seccomp filter denies socket-related syscalls and
the nftables firewall has no grant for that UID. With `outbound=true`, the
manager adds the dedicated UID to the nftables outbound set for the duration of
the app launch. `namespaces=true` and `inbound=true` fail closed because those
controls are not implemented. App launches are currently explicit through
`--run-app`; normal service/IPC launch integration is pending.

### QEMU demo results

- App A read its declared resource: allowed.
- App A read App B's resource: denied by Landlock.
- App A wrote its private directory: allowed.
- App A socket creation: denied by seccomp.
- App B read its declared resource and wrote its private directory: allowed.
- App B read App A's resource: denied by Landlock.
- App B DNS with its configured outbound grant: allowed.
- App B's outbound UID grant was removed after it exited.

These tests ran against the ARM64 QEMU image with the actual kernel mechanisms.
They do not prove isolation against privileged processes or replace device
hardware testing.

## Implemented files

- Kernel: `buildroot/board/qemu/aarch64-virt/linux.config`.
- User identities: `buildroot/board/qemu/algae-users.table`.
- Runtime sysctls: `rootfs/overlay/etc/sysctl.d/99-algae-security.conf`.
- Firewall: `rootfs/overlay/etc/nftables.conf`.
- App policies: `rootfs/overlay/etc/algae/permissions/`.
- Manager and enforcement: `system/service-manager/src/main.rs`.
- Demo apps: `system/security-demo/`.
- ARM64 builder: `scripts/build-security-components.sh`.

## Remaining work

1. Integrate app-policy launches with the service manager's normal service and
   IPC lifecycle; guarantee temporary network grants are cleaned up if the
   manager or app is forcibly terminated.
2. Implement per-service namespaces and cgroup resource limits; requests for
   these currently fail closed.
3. Implement per-app inbound network permissions and test essential networking
   under the complete policy.
4. Add structured security events for denied operations and timestamps without
   storing sensitive application data.
5. Move writable state out of the system root, verify startup dependencies, and
   test read-only root operation.
6. Re-run GUI and security checks together as the remaining controls are added.
