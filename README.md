# uniOS

![uniOS Screenshot](docs/screenshot.png)

> **A minimal, 64-bit operating system kernel built from scratch.**

![License](https://img.shields.io/badge/license-MIT-blue.svg?style=flat-square)
![Platform](https://img.shields.io/badge/platform-x86__64-lightgrey.svg?style=flat-square)
![Language](https://img.shields.io/badge/language-C%2B%2B20-orange.svg?style=flat-square)

**uniOS** is a Minimalist x86 OS in C++20. It serves as a clean, modern, and hackable educational resource for understanding operating system internals, featuring a custom kernel, native drivers, and a transparent design philosophy.

Current Version: **v0.3.0**

---

## Features

*   **Modern Core**: Custom C++20 kernel with minimal assembly stubs.
*   **Networking**: Full TCP/IP stack (Ethernet, ARP, IPv4, ICMP, UDP, TCP, DHCP, DNS).
*   **Driver Support**:
    *   **Network**: Intel e1000/I217/I218/I219/I225, Realtek RTL8139.
    *   **USB**: Native xHCI driver with HID support (Keyboard & Mouse).
    *   **Input**: PS/2 Keyboard & Mouse.
*   **Memory Management**: Robust PMM (Bitmap), VMM (Page Tables), and Kernel Heap (Bucket Allocator).
*   **Boot Protocol**: Powered by **Limine** (v8.x) for a seamless, quiet boot experience.
*   **Visuals**: Direct framebuffer access with custom font rendering and a sleek dark theme.
*   **Interactive Shell**: Command history, line editing, and network commands (`ping`, `dhcp`, `ifconfig`).

## Development & Testing

uniOS is written to be **portable across all x86_64 machines**, but is primarily tested on **QEMU/KVM**. This allows for rapid development with full debugging support (GDB, snapshots, device inspection).

> **Note**: Real hardware support is a future goal. The OS *may* work on physical machines, but stability is not guaranteed until dedicated hardware testing is implemented.

## Getting Started

### Prerequisites
*   `gcc` (cross-compiler for `x86_64-elf`)
*   `nasm`
*   `xorriso`
*   `qemu-system-x86_64`

### Build & Run

```bash
# Clone the repository
git clone https://github.com/unionyxx/uniOS.git
cd uniOS

# Build Limine (one-time setup)
git clone https://github.com/limine-bootloader/limine.git --branch=v8.x-binary --depth=1
make -C limine

# Compile and emulate
make run
```

## Structure

| Directory | Description |
|-----------|-------------|
| `kernel/core` | Core kernel logic (kmain, debug, scheduler). |
| `kernel/arch` | Architecture-specific code (GDT, IDT, interrupts). |
| `kernel/mem` | Memory management (PMM, VMM, Heap). |
| `kernel/drivers` | Device drivers (PCI, Timer, Graphics, Input). |
| `kernel/net` | Network stack implementation. |
| `kernel/fs` | Filesystem support. |
| `kernel/shell` | Kernel shell implementation. |
| `boot/` | Bootloader configuration files. |
| `libc/` | Custom C standard library implementation. |
| `userspace/` | User-mode applications and shells. |

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
