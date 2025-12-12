# Makefile for uniOS
# ==================
# Build: make [release|debug]  (default: release)
# Run:   make run[-debug|-serial|-gdb]

# Toolchain
CXX = g++
LD = ld

# Directories
KERNEL_DIRS = kernel/core kernel/arch kernel/mem kernel/drivers kernel/drivers/net kernel/drivers/usb kernel/net kernel/fs kernel/shell

# Build configuration (default: release)
BUILD ?= release

# Base flags (always applied)
CXXFLAGS_BASE = -std=c++20 -ffreestanding -fno-exceptions -fno-rtti \
                -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mno-80387 \
                -Wall -Wextra -Wno-volatile \
                -I. -Ikernel $(foreach dir,$(KERNEL_DIRS),-I$(dir))

# Debug-specific flags
CXXFLAGS_DEBUG = $(CXXFLAGS_BASE) -DDEBUG -g -O0

# Release-specific flags (no debug output, optimized)
CXXFLAGS_RELEASE = $(CXXFLAGS_BASE) -DNDEBUG -O2

# Select flags based on build type
ifeq ($(BUILD),debug)
    CXXFLAGS = $(CXXFLAGS_DEBUG)
else
    CXXFLAGS = $(CXXFLAGS_RELEASE)
endif

LDFLAGS = -nostdlib -T kernel/linker.ld -z max-page-size=0x1000

# Files
KERNEL_SRC = $(foreach dir,$(KERNEL_DIRS),$(wildcard $(dir)/*.cpp))
KERNEL_ASM = $(foreach dir,$(KERNEL_DIRS),$(wildcard $(dir)/*.asm))
KERNEL_OBJ = $(KERNEL_SRC:.cpp=.o) $(KERNEL_ASM:.asm=.o)
KERNEL_BIN = kernel.elf
ISO_IMAGE = uniOS.iso

# QEMU options
QEMU = qemu-system-x86_64
QEMU_BASE = -cdrom $(ISO_IMAGE) -m 512M
QEMU_NET = -nic user,model=e1000
QEMU_SERIAL = -serial stdio
QEMU_DEBUG = -s -S

# ==============================================================================
# Build Targets
# ==============================================================================

.PHONY: all release debug clean run run-net run-serial run-gdb help

all: release

release:
	@$(MAKE) BUILD=release iso

debug:
	@$(MAKE) BUILD=debug iso

iso: $(ISO_IMAGE)

$(KERNEL_BIN): $(KERNEL_OBJ)
	$(LD) $(LDFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.asm
	nasm -f elf64 $< -o $@

$(ISO_IMAGE): $(KERNEL_BIN) limine.conf
	@echo "Building ISO ($(BUILD) build)..."
	@python3 tools/mkunifs.py rootfs unifs.img
	@rm -rf iso_root
	@mkdir -p iso_root
	@cp $(KERNEL_BIN) iso_root/
	@cp unifs.img iso_root/
	@cp limine.conf iso_root/
	@cp limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso_root/
	@cp limine/limine.sys iso_root/ 2>/dev/null || true
	@xorriso -as mkisofs -b limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(ISO_IMAGE) 2>/dev/null
	@./limine/limine bios-install $(ISO_IMAGE) 2>/dev/null
	@echo "Done: $(ISO_IMAGE)"

# ==============================================================================
# Run Targets
# ==============================================================================

run: $(ISO_IMAGE)
	$(QEMU) $(QEMU_BASE)

run-net: $(ISO_IMAGE)
	$(QEMU) $(QEMU_BASE) $(QEMU_NET)

run-serial: $(ISO_IMAGE)
	$(QEMU) $(QEMU_BASE) $(QEMU_SERIAL)

run-gdb: $(ISO_IMAGE)
	@echo "Waiting for GDB on localhost:1234..."
	$(QEMU) $(QEMU_BASE) $(QEMU_DEBUG)

# ==============================================================================
# Utility Targets
# ==============================================================================

clean:
	rm -f $(KERNEL_OBJ) $(KERNEL_BIN) $(ISO_IMAGE)
	rm -rf iso_root

help:
	@echo "uniOS Build System"
	@echo "=================="
	@echo ""
	@echo "Build targets:"
	@echo "  make           - Build release version (default)"
	@echo "  make release   - Build optimized release version"
	@echo "  make debug     - Build debug version with DEBUG macro"
	@echo ""
	@echo "Run targets:"
	@echo "  make run       - Run in QEMU"
	@echo "  make run-net   - Run with e1000 network"
	@echo "  make run-serial- Run with serial output to stdio"
	@echo "  make run-gdb   - Run with GDB stub (localhost:1234)"
	@echo ""
	@echo "Utility:"
	@echo "  make clean     - Remove build artifacts"
	@echo "  make help      - Show this help"
