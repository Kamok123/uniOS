# uniOS Development Roadmap

## Current State ✅

| Component | Status |
|-----------|--------|
| Kernel Boot (Limine) | ✅ Complete |
| GDT/IDT/TSS | ✅ Complete |
| Memory (PMM/VMM/Heap) | ✅ Complete |
| Multitasking | ✅ Complete |
| Filesystem (uniFS) | ✅ Complete |
| Shell (uniSH) | ✅ Complete |
| User Mode (Ring 3) | ✅ Complete |
| ELF Loader | ✅ Complete |
| Syscalls | ✅ Complete |
| IPC (Pipes) | ✅ Complete |
| Basic GUI | ✅ Complete |

---

## Phase 16: Window Manager
**Priority: High | Effort: Large**

### Goals
- Draggable, resizable windows
- Window decorations (title bar, close/minimize buttons)
- Window stacking (z-order)
- Focus management

### Tasks
1. **Window Structure**
   - Window ID, position, size, title
   - Window state (normal, minimized, maximized)
   - Per-window framebuffer

2. **Window Operations**
   - `window_create(x, y, w, h, title)`
   - `window_destroy(id)`
   - `window_move(id, x, y)`
   - `window_resize(id, w, h)`

3. **Compositor**
   - Double-buffering for flicker-free rendering
   - Dirty rectangle optimization
   - Window clipping

4. **Input Routing**
   - Mouse click → determine focused window
   - Keyboard → route to focused window

---

## Phase 17: Storage Driver (ATA/IDE)
**Priority: High | Effort: Medium**

### Goals
- Read/write sectors from hard disk
- Persistent storage for files

### Tasks
1. **PIO Mode Driver**
   - Detect ATA drives
   - Read sectors (LBA28/LBA48)
   - Write sectors

2. **Partition Table**
   - MBR parsing
   - GPT support (optional)

3. **Integration**
   - Mount disk partitions
   - Persistent uniFS or simple FAT16

---

## Phase 18: Networking Stack
**Priority: Medium | Effort: Very Large**

### Goals
- Basic network connectivity
- TCP/IP stack

### Tasks
1. **NIC Driver** (e1000 or RTL8139 for QEMU)
   - Packet send/receive
   - Interrupt handling

2. **Protocol Stack**
   - Ethernet framing
   - ARP
   - IPv4
   - ICMP (ping)
   - UDP
   - TCP

3. **Sockets API**
   - `socket()`, `bind()`, `listen()`, `accept()`
   - `connect()`, `send()`, `recv()`

---

## Phase 19: USB Support
**Priority: Medium | Effort: Large**

### Goals
- USB keyboard/mouse support
- Better QEMU integration (USB tablet for absolute pointing)

### Tasks
1. **UHCI/OHCI Controller**
   - Controller detection
   - Transfer descriptors

2. **USB Core**
   - Device enumeration
   - Configuration parsing
   - Endpoint management

3. **HID Class Driver**
   - USB keyboard
   - USB mouse/tablet

---

## Phase 20: Sound Support
**Priority: Low | Effort: Medium**

### Goals
- Basic audio output

### Tasks
1. **Sound Blaster 16** (easy, QEMU supported)
   - DMA-based audio playback
   - Simple beep/tone generation

2. **Intel HDA** (harder, more modern)
   - Codec detection
   - Stream management

---

## Phase 21: Advanced Process Management
**Priority: High | Effort: Medium**

### Goals
- True multi-process with fork/exec
- Process isolation

### Tasks
1. **Fork Implementation**
   - Copy page tables (copy-on-write optional)
   - Duplicate file descriptors
   - Return PID to parent, 0 to child

2. **Exec Implementation**
   - Replace process image with new ELF
   - Reset stack and registers

3. **Wait/Exit**
   - `waitpid()` syscall
   - Zombie process handling
   - Process cleanup

4. **Signals**
   - SIGTERM, SIGKILL, SIGINT
   - Signal handlers

---

## Phase 22: GUI Applications
**Priority: Medium | Effort: Medium**

### Goals
- User-space GUI programs
- Widget toolkit

### Tasks
1. **GUI Syscalls**
   - Window creation from user space
   - Drawing primitives
   - Event queue

2. **Simple Apps**
   - File manager
   - Text editor
   - Terminal emulator
   - Calculator

---

## Phase 23: C Standard Library
**Priority: High | Effort: Large**

### Goals
- Minimal libc for user programs
- Easier program development

### Tasks
1. **Core Functions**
   - `printf`, `sprintf`
   - `malloc`, `free`
   - `memcpy`, `memset`, `strlen`, `strcmp`

2. **File I/O**
   - `fopen`, `fread`, `fwrite`, `fclose`

3. **Process**
   - `exit`, `getpid`

---

## Recommended Order

```
Phase 21 (Process Management) → Phase 23 (libc)
    ↓
Phase 17 (Storage) → Phase 16 (Window Manager)
    ↓
Phase 22 (GUI Apps)
    ↓
Phase 18 (Networking) or Phase 19 (USB)
```

---

## Quick Wins (Can Do Anytime)

| Feature | Effort | Impact |
|---------|--------|--------|
| RTC (Real-Time Clock) | Small | Show current time |
| VGA Text Mode | Small | Alternative to framebuffer |
| Kernel Panic Screen | Small | Better crash debugging |
| Stack Trace on Crash | Medium | Debug user programs |
| `/proc` filesystem | Medium | Process introspection |
| Environment Variables | Small | `getenv`/`setenv` |
| Command History in Shell | Small | Arrow keys recall |
| Tab Completion | Medium | Better UX |

---

## Architecture Improvements

1. **Kernel Modules** - Loadable drivers
2. **VFS Layer** - Abstract filesystem interface
3. **ACPI Support** - Power management, hardware discovery
4. **SMP** - Multi-core CPU support
5. **64-bit Long Mode Cleanup** - Proper canonical addresses
