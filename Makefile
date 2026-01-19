# ============================================================================
# AEOS - Abdalla's Educational Operating System
# File: Makefile
# Description: Main build system for AEOS kernel
# ============================================================================

# Toolchain configuration
CROSS_COMPILE ?= aarch64-linux-gnu-
AS      = $(CROSS_COMPILE)as
CC      = $(CROSS_COMPILE)gcc
LD      = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump
M4      = m4

# Directories
SRC_DIR     = src
BUILD_DIR   = build
INCLUDE_DIR = include

# Compiler flags
CFLAGS  = -Wall -Wextra -Werror -nostdlib -ffreestanding -fno-builtin
CFLAGS += -mcpu=cortex-a57 -march=armv8-a
CFLAGS += -O2 -g
CFLAGS += -I$(INCLUDE_DIR)

# Debug mode (use DEBUG=1 make run to enable debug messages)
ifeq ($(DEBUG),1)
CFLAGS += -DDEBUG_ENABLED
endif

# Assembler flags
ASFLAGS = -mcpu=cortex-a57 -g

# Linker flags
LDFLAGS = -T linker.ld -nostdlib

# Source files
ASM_SOURCES = src/boot/boot.asm \
              src/interrupts/vectors.asm \
              src/proc/context.asm
C_SOURCES   = src/kernel/main.c \
              src/kernel/kprintf.c \
              src/kernel/shell.c \
              src/kernel/editor.c \
              src/drivers/uart.c \
              src/drivers/framebuffer.c \
              src/drivers/dtb.c \
              src/drivers/ramfb.c \
              src/drivers/virtio_gpu.c \
              src/drivers/pflash.c \
              src/drivers/semihosting.c \
              src/mm/mm.c \
              src/mm/pmm.c \
              src/mm/heap.c \
              src/interrupts/exceptions.c \
              src/interrupts/gic.c \
              src/interrupts/timer.c \
              src/proc/process.c \
              src/proc/scheduler.c \
              src/syscall/syscall.c \
              src/fs/vfs.c \
              src/fs/ramfs.c \
              src/fs/fs_persist.c \
              src/lib/string.c

# Object files
ASM_OBJECTS = $(patsubst src/%.asm,$(BUILD_DIR)/%.o,$(ASM_SOURCES))
C_OBJECTS   = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
ALL_OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

# Output files
KERNEL_ELF = kernel.elf
KERNEL_BIN = kernel.bin
KERNEL_IMG = kernel.img
PFLASH_IMG = persist.bin

# Phony targets
.PHONY: all clean run debug dump directories pflash

# Default target
all: directories $(KERNEL_ELF) $(KERNEL_BIN) pflash

# Create pflash files (64MB each, QEMU needs both banks)
pflash:
	@if [ ! -f flash0.img ]; then \
		echo "Creating pflash bank 0 (64MB, read-only firmware slot)..."; \
		dd if=/dev/zero of=flash0.img bs=1M count=64 2>/dev/null; \
	fi
	@if [ ! -f $(PFLASH_IMG) ]; then \
		echo "Creating pflash persistence file (64MB)..."; \
		dd if=/dev/zero of=$(PFLASH_IMG) bs=1M count=64 2>/dev/null; \
	fi

# Create build directories
directories:
	@mkdir -p $(BUILD_DIR)/boot
	@mkdir -p $(BUILD_DIR)/kernel
	@mkdir -p $(BUILD_DIR)/drivers
	@mkdir -p $(BUILD_DIR)/mm
	@mkdir -p $(BUILD_DIR)/interrupts
	@mkdir -p $(BUILD_DIR)/proc
	@mkdir -p $(BUILD_DIR)/syscall
	@mkdir -p $(BUILD_DIR)/fs
	@mkdir -p $(BUILD_DIR)/lib

# Build kernel ELF
$(KERNEL_ELF): $(ALL_OBJECTS)
	@echo "Linking kernel..."
	$(LD) $(LDFLAGS) $(ALL_OBJECTS) -o $@
	@echo "Kernel linked successfully: $@"

# Create raw binary
$(KERNEL_BIN): $(KERNEL_ELF)
	@echo "Creating raw binary..."
	$(OBJCOPY) -O binary $< $@

# Compile assembly files (with m4 preprocessing)
$(BUILD_DIR)/%.o: src/%.asm
	@echo "Assembling $<..."
	@mkdir -p $(dir $@)
	$(M4) $< > $(BUILD_DIR)/$*.s
	$(AS) $(ASFLAGS) $(BUILD_DIR)/$*.s -o $@

# Compile C files
$(BUILD_DIR)/%.o: src/%.c
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR)
	rm -f $(KERNEL_ELF) $(KERNEL_BIN) $(KERNEL_IMG)
	@echo "Clean complete"

# Run in QEMU (text mode with semihosting for persistence)
run: all
	@echo "Starting QEMU (text mode with semihosting)..."
	@echo "Filesystem will be saved to 'aeos_fs.img' on host when you run 'save' command"
	qemu-system-aarch64 -M virt -cpu cortex-a57 -m 256M \
		-nographic -kernel $(KERNEL_ELF) \
		-semihosting-config enable=on,target=native

# Run without semihosting (no persistence)
run-nopersist: all
	@echo "Starting QEMU (text mode, no persistence)..."
	qemu-system-aarch64 -M virt -cpu cortex-a57 -m 256M \
		-nographic -kernel $(KERNEL_ELF)

# Run with graphics (using VirtIO GPU MMIO device)
run-ramfb: all
	@echo "Starting QEMU with graphics window..."
	@echo "Graphics will appear in a separate window"
	@echo "Press Ctrl+Alt+G to release mouse/keyboard"
	qemu-system-aarch64 -M virt -cpu cortex-a57 -m 256M \
		-device virtio-gpu-device \
		-device virtio-keyboard-device \
		-device virtio-mouse-device \
		-serial stdio \
		-semihosting-config enable=on,target=native \
		-kernel $(KERNEL_ELF)

# Alternative: Try with simpler ramfb device (works with fw_cfg if available)
run-simple: all
	@echo "Starting QEMU with simple framebuffer (experimental)..."
	qemu-system-aarch64 -M virt -cpu cortex-a57 -m 256M \
		-device ramfb \
		-serial stdio \
		-semihosting-config enable=on,target=native \
		-kernel $(KERNEL_ELF)

# Take screenshot of framebuffer (works in any mode)
screenshot: all
	@echo "Starting QEMU and taking screenshot after 3 seconds..."
	@echo "Screenshot will be saved as aeos_screen.ppm"
	qemu-system-aarch64 -M virt -cpu cortex-a57 -m 256M \
		-device virtio-gpu-device \
		-serial stdio \
		-kernel $(KERNEL_ELF) & \
	sleep 3 && \
	echo "Taking screenshot..." && \
	echo "screendump aeos_screen.ppm" | nc -U /tmp/qemu-monitor.sock || \
	echo "Screenshot failed - QEMU monitor not available"

# Run with ramfb + VNC output
run-vnc: all
	@echo "Starting QEMU with ramfb (VNC output)..."
	@echo "Connect VNC client to localhost:5900"
	@echo "Serial output will appear in terminal"
	qemu-system-aarch64 -M virt -cpu cortex-a57 -m 256M \
		-device ramfb \
		-vnc :0 \
		-serial stdio \
		-semihosting-config enable=on,target=native \
		-kernel $(KERNEL_ELF)

# Run with virtio-gpu (advanced GPU driver)
run-virtio: all
	@echo "Starting QEMU with virtio-gpu..."
	@echo "Graphics will appear in a separate window"
	@echo "Press Ctrl+Alt+G to release mouse/keyboard"
	qemu-system-aarch64 -M virt -cpu cortex-a57 -m 256M \
		-device virtio-gpu-device \
		-serial stdio \
		-semihosting-config enable=on,target=native \
		-kernel $(KERNEL_ELF)

# Run with both ramfb AND virtio-gpu
run-all-gpu: all
	@echo "Starting QEMU with ALL GPU devices..."
	@echo "Graphics will appear in a separate window"
	qemu-system-aarch64 -M virt -cpu cortex-a57 -m 256M \
		-device ramfb \
		-device virtio-gpu-device \
		-serial stdio \
		-semihosting-config enable=on,target=native \
		-kernel $(KERNEL_ELF)

# Legacy GUI alias
run-gui: run-ramfb

# Run with fresh filesystem (no saved state)
run-clean:
	@echo "Building kernel with fresh filesystem..."
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) -DFS_NO_LOAD"
	@echo "Starting QEMU (fresh filesystem, no saved state)..."
	qemu-system-aarch64 -M virt -cpu cortex-a57 -m 256M \
		-nographic -kernel $(KERNEL_ELF)

# Run with GDB debugging
debug: all
	@echo "Starting QEMU with GDB server..."
	@echo "Connect with: aarch64-linux-gnu-gdb kernel.elf -ex 'target remote :1234'"
	qemu-system-aarch64 -M virt -cpu cortex-a57 -m 256M \
		-nographic -kernel $(KERNEL_ELF) -S -s

# Disassemble kernel
dump: $(KERNEL_ELF)
	@echo "Disassembling kernel..."
	$(OBJDUMP) -d $(KERNEL_ELF) > kernel.asm
	@echo "Disassembly saved to kernel.asm"

# Display help
help:
	@echo "AEOS Build System"
	@echo ""
	@echo "Build Targets:"
	@echo "  all      - Build kernel (default)"
	@echo "  clean    - Remove build artifacts"
	@echo "  dump     - Disassemble kernel to kernel.asm"
	@echo ""
	@echo "Run Targets (Text Mode):"
	@echo "  run         - Text mode with semihosting (saves to aeos_fs.img)"
	@echo "  run-nopersist - Text mode without persistence"
	@echo "  run-clean   - Fresh filesystem (no saved state)"
	@echo "  debug       - Run with GDB server (text mode)"
	@echo ""
	@echo "Run Targets (Graphical Modes):"
	@echo "  run-ramfb    - Simple framebuffer in SDL window"
	@echo "  run-vnc      - Simple framebuffer via VNC (port 5900)"
	@echo "  run-virtio   - VirtIO GPU in SDL window"
	@echo "  run-all-gpu  - Both ramfb and virtio-gpu"
	@echo "  run-gui      - Alias for run-ramfb"
	@echo ""
	@echo "Filesystem Persistence:"
	@echo "  The 'save' command saves the filesystem to 'aeos_fs.img' on host"
	@echo "  Next boot will automatically load 'aeos_fs.img' if it exists"
	@echo ""
	@echo "Configuration:"
	@echo "  CROSS_COMPILE - Toolchain prefix (default: aarch64-linux-gnu-)"
	@echo ""
	@echo "Examples:"
	@echo "  make run          # Text mode with persistence"
	@echo "  make run-ramfb    # Graphical window (easiest)"
	@echo "  make run-vnc      # VNC server on port 5900"

# ============================================================================
# End of Makefile
# ============================================================================
