# Algae OS

<p align="center">
  <strong>Grow your own ecosystem.</strong>
</p>

A privacy-focused, Linux-based mobile operating system built with Rust.

Algae OS is an independent mobile operating system project built on the Linux kernel, with Rust-powered system components and a lightweight graphical environment.

The long-term vision is to create an independent mobile platform that prioritizes privacy, security, performance, user control, and freedom from dependence on Google's proprietary mobile ecosystem.

**Repository:** https://github.com/kowsiganarunachalam/algae-os

---

## Table of Contents

- [Project Overview](#project-overview)
- [Project Status](#project-status)
- [Technology Stack](#technology-stack)
- [System Architecture](#system-architecture)
- [Supported Development Platforms](#supported-development-platforms)
- [Getting Started](#getting-started)
  - [Arch Linux](#arch-linux)
  - [Ubuntu](#ubuntu)
  - [Native Windows](#native-windows)
- [Buildroot Installation](#buildroot-installation)
- [Configure Algae OS](#configure-algae-os)
- [Building Algae OS](#building-algae-os)
- [Running Algae OS in QEMU](#running-algae-os-in-qemu)
- [Service Management](#service-management)
- [Project Structure](#project-structure)
- [Troubleshooting](#troubleshooting)
- [Development Roadmap](#development-roadmap)
- [Current Limitations](#current-limitations)
- [Contributing](#contributing)
- [Project Vision](#project-vision)

---

# Project Overview

Algae OS is an experimental Linux-based mobile operating system.

Unlike conventional Android-based projects, Algae OS aims to establish its own system architecture, graphical environment, service management, and application ecosystem.

The project uses Rust for system-level development wherever practical, while leveraging the Linux kernel for hardware and process management.

Development currently focuses on ARM64 virtualization using QEMU before moving toward physical mobile hardware.

## Project Status

**Current milestone: ARM64 graphical environment running in QEMU.**

Currently implemented:

- Linux kernel booting on ARM64.
- Buildroot-based root filesystem.
- QEMU ARM64 virtual machine support.
- Weston Wayland compositor.
- Virtio GPU integration.
- Algae-branded graphical home screen.
- Animated algae-shaped startup indicator.
- Rust-based service manager.
- Service management through the serial console.
- BusyBox init running as PID 1.

Algae OS is an early development prototype, not a production-ready mobile operating system.

---

# Technology Stack

| Component | Technology |
|---|---|
| Kernel | Linux |
| System programming | Rust |
| Architecture | ARM64 / AArch64 |
| Root filesystem | Buildroot |
| Virtualization | QEMU |
| Display compositor | Weston |
| Display protocol | Wayland |
| Graphics | Cairo |
| Device management | eudev |
| Initial process | BusyBox init |
| Service management | Rust |
| Graphical interface | Algae Home (C, Cairo, Wayland) |
| Future UI framework | Slint |

---

# System Architecture

The current architecture separates Linux system initialization, service management, and graphical output.

```text
                 Algae OS
                     |
                     v
                Linux Kernel
                     |
                     v
              BusyBox Init (PID 1)
                     |
           +---------+----------+
           |                    |
           v                    v
     Rust Service          Graphical
       Manager             Environment
           |                    |
           v                    v
    System Services           Weston
                                |
                                v
                             Wayland
                                |
                                v
                            Algae Home
```

BusyBox init remains responsible for initial system startup. The Rust service manager operates as a separate background process.

---

# Supported Development Platforms

Algae OS supports different development workflows depending on the host operating system.

| Platform | Rust development | Full OS build | QEMU testing |
|---|---|---|---|
| Arch Linux | Yes | Yes | Yes |
| Ubuntu | Yes | Yes | Yes |
| Native Windows | Yes | Not currently supported | Yes, with a compatible image |
| Windows + WSL2 | Yes | Possible through Linux environment | Yes |

**Note:** Buildroot is designed for Linux host environments. Native Windows users can develop portable Rust components and graphical interfaces, but full Buildroot image generation currently requires Linux.

---

# Getting Started

## Arch Linux

Arch Linux is the primary development environment for Algae OS.

### 1. Update the system

```bash
sudo pacman -Syu
```

### 2. Install dependencies

```bash
sudo pacman -S --needed \
    base-devel \
    git \
    wget \
    curl \
    cpio \
    unzip \
    rsync \
    bc \
    bison \
    flex \
    ncurses \
    openssl \
    python \
    perl \
    file \
    which \
    diffutils \
    findutils \
    qemu-system-aarch64 \
    qemu-img \
    tigervnc
```

### 3. Install Rust

If Rust is not already installed:

```bash
sudo pacman -S --needed rustup
rustup default stable
```

Verify:

```bash
rustc --version
cargo --version
```

### 4. Verify QEMU

```bash
qemu-system-aarch64 --version
```

---

## Ubuntu

Ubuntu is another supported Linux development environment.

### 1. Update packages

```bash
sudo apt update
sudo apt upgrade -y
```

### 2. Install dependencies

```bash
sudo apt install -y \
    build-essential \
    git \
    wget \
    curl \
    cpio \
    unzip \
    rsync \
    bc \
    bison \
    flex \
    libncurses-dev \
    libssl-dev \
    python3 \
    perl \
    file \
    which \
    diffutils \
    findutils \
    qemu-system-arm \
    qemu-utils \
    tigervnc-viewer
```

### 3. Install Rust

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

Reload the environment:

```bash
source "$HOME/.cargo/env"
```

Verify:

```bash
rustc --version
cargo --version
```

### 4. Verify QEMU

```bash
qemu-system-aarch64 --version
```

---

## Native Windows

Algae OS supports native Windows development for portable Rust components, graphical interface development, documentation, and QEMU-based testing.

**No WSL2, Linux subsystem, or virtual Linux distribution is required for these development tasks.**

### Requirements

- Windows 10 or Windows 11 (64-bit).
- Git for Windows.
- Rustup.
- Visual Studio Build Tools.
- Visual Studio Code.
- QEMU for Windows.

### 1. Install Git

Download and install:

https://git-scm.com/download/win

Verify in PowerShell:

```powershell
git --version
```

### 2. Install Rust

Download:

https://rustup.rs/

Run the installer and select the default installation.

Verify:

```powershell
rustc --version
cargo --version
```

### 3. Install Visual Studio Build Tools

Download:

https://visualstudio.microsoft.com/visual-cpp-build-tools/

Install the **Desktop development with C++** workload.

This provides the compiler and native development tools required by some Rust packages.

### 4. Install Visual Studio Code

Download:

https://code.visualstudio.com/

Recommended extensions:

- rust-analyzer
- CodeLLDB
- Even Better TOML

### 5. Install QEMU

Download:

https://www.qemu.org/download/#windows

Install the Windows version and add its installation directory to PATH if necessary.

Verify:

```powershell
qemu-system-aarch64 --version
```

### 6. Clone Algae OS

Open PowerShell:

```powershell
mkdir C:\Projects
cd C:\Projects
```

Clone the repository:

```powershell
git clone https://github.com/kowsiganarunachalam/algae-os.git
```

Enter the project:

```powershell
cd algae-os
```

Open VS Code:

```powershell
code .
```

### 7. Develop Rust components

Navigate to the Rust service manager:

```powershell
cd system\service-manager
```

Build and test portable Windows-compatible Rust code:

```powershell
cargo build
cargo test
```

Some system components depend on Linux-specific APIs and cannot run as native Windows applications without platform-specific implementations.

### 8. QEMU testing

Windows QEMU can emulate ARM64 machines and run compatible Algae OS images.

However, the existing graphical launcher is a Bash script:

```bash
./scripts/run-qemu-graphical.sh
```

It cannot be executed directly from PowerShell.

A native Windows PowerShell launcher will be needed to reproduce the existing QEMU configuration.

The exact QEMU arguments must match the generated Linux kernel, root filesystem, and virtual hardware configuration.

### Native Windows limitations

- Full Buildroot image generation is not currently supported.
- Linux kernel compilation requires a supported Linux build environment.
- Linux-specific Rust services cannot necessarily run as native Windows applications.
- Existing Bash scripts need PowerShell equivalents.
- QEMU testing requires a compatible, already-generated Algae OS image.

---

# Buildroot Installation

Buildroot generates the Linux root filesystem and integrates the packages required by Algae OS.

This section applies to Arch Linux and Ubuntu.

### 1. Clone Buildroot

First, enter the Algae OS project root.

Clone Buildroot:

```bash
git clone https://github.com/buildroot/buildroot.git
```

This creates the `buildroot/` directory inside the Algae OS project.

### 2. Check Buildroot version

```bash
git -C buildroot describe --tags --always
```

For reproducible builds, Algae OS should use a tested and pinned Buildroot release or commit.

---

# Configure Algae OS

Algae OS includes a dedicated Buildroot configuration for its ARM64 QEMU target.

The configuration includes:

- ARM64 QEMU virtual hardware.
- Linux kernel 6.18.7 configuration.
- Virtio GPU support through the QEMU kernel configuration.
- eudev device management.
- Weston compositor.
- Early framebuffer splash using psplash.
- Algae OS root filesystem overlay.
- EXT4 root filesystem.
- Buildroot host QEMU.

From the Algae OS project root, execute:

```bash
make -C buildroot BR2_DEFCONFIG="$PWD/algae_qemu_defconfig" defconfig
```

This imports the Algae OS configuration into Buildroot.

---

# Building Algae OS

**Important: Algae OS uses a two-stage build process for its graphical home application.**

The first Buildroot build generates the cross-compilation toolchain and dependencies required by the Algae Home build script.

The second Buildroot build incorporates the generated graphical application into the final image.

## Stage 1 – Initial Buildroot build

Run:

```bash
make -C buildroot -j2
```

This builds the configured system components and generates the ARM64 cross-compilation environment.

The first build may take considerable time because Buildroot downloads and compiles:

- Cross-compilation toolchain.
- Linux kernel.
- BusyBox.
- System libraries.
- Weston.
- Wayland components.
- Device management utilities.
- Host tools.
- Other configured packages.

## Stage 2 – Build Algae Home

After the first Buildroot build completes successfully, run:

```bash
./scripts/build-algae-home.sh
```

This compiles the Algae graphical home application using the generated ARM64 toolchain and required graphical dependencies.

## Stage 3 – Rebuild the final image

Run Buildroot again:

```bash
make -C buildroot -j2
```

This incorporates the generated application into the root filesystem image.

## Build performance

The `-j2` option allows two compilation jobs to run simultaneously.

To use more CPU cores:

```bash
make -C buildroot -j$(nproc)
```

Choose the number of parallel jobs according to available RAM and CPU resources.

**Important:** Avoid deleting the Buildroot output directory between ordinary builds. Buildroot reuses previously compiled components, making subsequent builds faster.

---

# Running Algae OS in QEMU

After the final build completes, launch the ARM64 virtual machine.

### 1. Start the virtual machine

Open Terminal 1:

```bash
./scripts/run-qemu-graphical.sh
```

The launcher uses Buildroot's generated QEMU binary and keeps the serial console attached to the terminal.

### 2. Connect to the graphical display

Open Terminal 2:

```bash
vncviewer 127.0.0.1:0
```

The VNC viewer connects to the virtual machine's graphical output.

### 3. Expected startup sequence

1. QEMU starts the ARM64 virtual machine.
2. Linux kernel boots.
3. BusyBox init initializes the system.
4. Weston starts the graphical environment.
5. An animated algae-shaped loading indicator appears.
6. The Algae home screen opens.

The loading animation is expected to appear for approximately five seconds.

### 4. Change VNC display

If display 0 is occupied:

```bash
VNC_DISPLAY=1 ./scripts/run-qemu-graphical.sh
```

Connect using:

```bash
vncviewer 127.0.0.1:1
```

---

# Service Management

Algae OS includes a Rust-based service manager.

It currently runs as a separate background process while BusyBox init remains PID 1.

Service management is available through the serial console.

### Available commands

Check service status:

```bash
algae-service status
```

Stop a service:

```bash
algae-service stop demo-service
```

Start a service:

```bash
algae-service start demo-service
```

Restart a service:

```bash
algae-service restart demo-service
```

The service manager is an early implementation and will evolve as the operating system develops.

---

# Project Structure

The current repository contains the following top-level structure:

```text
algae-os/
│
├── rootfs/
│   └── overlay/               # Root filesystem overlay
│
├── rustos-init/               # Rust initialization component
│
├── scripts/
│   ├── build-algae-home.sh    # Build graphical home
│   └── run-qemu-graphical.sh  # Launch graphical QEMU
│
├── system/
│   ├── algae-home/            # Graphical home application
│   └── service-manager/       # Rust service manager
│
├── algae_qemu_defconfig       # ARM64 QEMU configuration
│
├── .gitignore
│
└── README.md
```

The `buildroot/` directory is cloned separately and excluded from the repository.

Generated Buildroot artifacts are kept outside version control.

---

# Troubleshooting

## Buildroot directory not found

If you see:

```text
make: *** buildroot: No such file or directory
```

Clone Buildroot:

```bash
git clone https://github.com/buildroot/buildroot.git
```

## Algae Home build fails

If the build script reports that the ARM64 compiler or graphical dependencies are missing, make sure the initial Buildroot build completed successfully.

Run:

```bash
make -C buildroot -j2
```

Then retry:

```bash
./scripts/build-algae-home.sh
```

## QEMU command not found

The project launcher uses the QEMU binary generated by Buildroot.

Check:

```bash
ls buildroot/output/host/bin/qemu-system-aarch64
```

If it does not exist, verify that the initial Buildroot build completed successfully.

## VNC connection refused

Check whether QEMU is running and whether the correct VNC display is being used.

For display 0:

```bash
vncviewer 127.0.0.1:0
```

For display 1:

```bash
vncviewer 127.0.0.1:1
```

## Graphical interface does not appear

Check:

- Kernel boot output.
- Virtio GPU support.
- Weston startup logs.
- QEMU VNC configuration.
- Root filesystem contents.
- Algae Home installation.

## Build takes too long

The first Buildroot build compiles many components from source.

Avoid cleaning the output directory unless a clean rebuild is necessary.

Subsequent builds generally reuse previously compiled components.

## Windows launcher unavailable

The current QEMU launcher is a Bash script.

Native Windows development will require a PowerShell equivalent that reproduces the same machine, kernel, root filesystem, graphics, VNC, and serial-console settings.

---

# Development Roadmap

Algae OS is being developed incrementally.

## Phase 1 – Core Operating System

- [x] Linux kernel integration.
- [x] ARM64 QEMU boot.
- [x] Buildroot root filesystem.
- [x] BusyBox init.
- [x] Initial Rust service manager.
- [x] Basic service control.

## Phase 2 – Graphical Environment

- [x] Weston integration.
- [x] Wayland graphical output.
- [x] QEMU virtio GPU support.
- [x] Algae startup animation.
- [x] Basic Algae home screen.

## Phase 3 – Native System Interface

- [ ] Slint graphical application integration.
- [ ] Native Algae UI framework.
- [ ] Application launcher.
- [ ] Settings application.
- [ ] Status bar.
- [ ] Notification system.
- [ ] Touch-oriented interface.

## Phase 4 – Mobile System Components

- [ ] Improved Rust service manager.
- [ ] System configuration management.
- [ ] Power management.
- [ ] Wi-Fi support.
- [ ] Bluetooth support.
- [ ] Audio subsystem.
- [ ] Display and brightness control.
- [ ] Touchscreen integration.

## Phase 5 – Real Hardware Support

- [ ] ARM64 device porting.
- [ ] Bootloader integration.
- [ ] Device-specific kernel configuration.
- [ ] Hardware driver integration.
- [ ] Storage and partition management.
- [ ] Battery management.

## Phase 6 – Privacy and Application Ecosystem

- [ ] Permission management.
- [ ] Application isolation.
- [ ] Secure application installation.
- [ ] Privacy dashboard.
- [ ] Native application ecosystem.
- [ ] Security auditing.

---

# Current Limitations

Algae OS is an experimental project.

Current limitations include:

- The graphical environment currently runs in QEMU.
- Weston provides the graphical display environment.
- The home screen is an early prototype.
- The Rust service manager has limited functionality.
- BusyBox init remains PID 1.
- Mobile hardware drivers are not yet integrated.
- Cellular connectivity and mobile telephony are not implemented.
- Production-grade power management is not implemented.
- Slint integration is planned but not yet completed.
- Native Windows full-system builds are not currently supported.

These limitations will be addressed progressively.

---

# Contributing

Contributions, testing, suggestions, and architectural discussions are welcome.

Before contributing:

1. Set up a supported development environment.
2. Build the existing QEMU image where supported.
3. Verify the graphical environment.
4. Understand the relevant system component.
5. Test changes before considering hardware deployment.
6. Submit changes through the project's contribution workflow.

---

# Design Philosophy

### Privacy First

Privacy should be a fundamental part of the operating system rather than an optional feature.

### Independence

Algae OS aims to develop its own mobile ecosystem without depending on Google's proprietary mobile services.

### Rust-Powered Components

Rust will be used wherever practical to develop reliable, memory-safe system software.

### Lightweight Architecture

The operating system should remain efficient and suitable for resource-constrained mobile hardware.

### Open Development

The project should evolve through transparent development, modular architecture, and community contributions.

---

# Project Vision

Algae OS aims to become an independent, lightweight, privacy-focused mobile operating system built on Linux and powered by Rust.

The objective is not simply to create another Linux distribution.

It is to develop an independent mobile platform with its own:

- Kernel and system integration.
- Service management.
- Graphical environment.
- Application framework.
- Privacy and security architecture.
- Application ecosystem.

**Algae OS — Grow your own ecosystem.**

---

## License

License information will be added when the project's licensing structure is finalized.

## Boot logo and styles

The default Algae boot animation uses the three-frond logo from the branding
reference. Choose **Settings** on the Algae home screen to select Logo, Kelp,
Pixel, or Sprig. The selection is stored in `/etc/algae/boot-style` and used by
the early framebuffer animation and the loading animation after Weston starts.
