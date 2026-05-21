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
# Forbid FP/SIMD codegen. CPACR_EL1 is not configured to allow Q-register access
# at EL1, so any vectorized integer code (e.g. paired uint64_t increments) traps.
CFLAGS += -mgeneral-regs-only
CFLAGS += -O2 -g
CFLAGS += -I$(INCLUDE_DIR)

# Debug mode (use DEBUG=1 make run to enable debug messages)
ifeq ($(DEBUG),1)
CFLAGS += -DDEBUG_ENABLED
endif

# TEST mode: link the in-kernel test runner (src/kernel/test_runner.c) as the
# kernel_main entry point instead of the normal main.c, so `make test` boots
# straight into the scenarios and exits via semihosting.
ifeq ($(TEST),1)
CFLAGS += -DTEST_BUILD
KERNEL_ENTRY_C = src/kernel/test_runner.c
else
KERNEL_ENTRY_C = src/kernel/main.c
endif

# Assembler flags
ASFLAGS = -mcpu=cortex-a57 -g

# Linker flags
LDFLAGS = -T linker.ld -nostdlib

# Source files
ASM_SOURCES = src/boot/boot.asm \
              src/interrupts/vectors.asm \
              src/proc/context.asm
C_SOURCES   = $(KERNEL_ENTRY_C) \
              src/kernel/kprintf.c \
              src/kernel/shell.c \
              src/kernel/editor.c \
              src/kernel/bootscreen.c \
              src/kernel/notify.c \
              src/kernel/event.c \
              src/kernel/window.c \
              src/kernel/wm.c \
              src/kernel/desktop.c \
              src/kernel/gui.c \
              src/kernel/stack_guard.c \
              src/drivers/uart.c \
              src/drivers/virtio_input.c \
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
              src/lib/string.c \
              src/lib/anim.c \
              src/apps/terminal.c \
              src/apps/filemanager.c \
              src/apps/settings.c \
              src/apps/about.c \
              src/apps/calculator.c \
              src/apps/sysmon.c \
              src/apps/notes.c \
              src/apps/tetris.c \
              src/kernel/backtrace.c

# Object files
ASM_OBJECTS = $(patsubst src/%.asm,$(BUILD_DIR)/%.o,$(ASM_SOURCES))
C_OBJECTS   = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
ALL_OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

# Generated symbol table for the in-kernel backtrace. See the "Symbol table
# two-pass build" comment near the link rules.
SYMBOLS_C       = $(BUILD_DIR)/kernel/symbol_data.c
SYMBOLS_STUB_C  = $(BUILD_DIR)/kernel/symbol_data_stub.c
SYMBOLS_OBJ     = $(BUILD_DIR)/kernel/symbol_data.o
SYMBOLS_STUB_O  = $(BUILD_DIR)/kernel/symbol_data_stub.o
KERNEL_STAGE1   = $(BUILD_DIR)/kernel-stage1.elf

# Output files
KERNEL_ELF = kernel.elf
KERNEL_BIN = kernel.bin
KERNEL_IMG = kernel.img
PFLASH_IMG = persist.bin

# Phony targets
.PHONY: all clean run debug dump directories pflash test audit

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
	@mkdir -p $(BUILD_DIR)/apps

# ----------------------------------------------------------------------------
# Symbol table two-pass build
#
# The in-kernel backtrace prints "<name>+0x<offset>" for each saved LR, which
# requires linking a sorted address-to-name table into the kernel itself.
# Linking that table changes the kernel image (the table lives in .rodata),
# so we cannot generate it from the same ELF we want to ship. The pipeline:
#
#   1. Link kernel-stage1.elf with an empty stub symbol table, just to get
#      stable addresses for every function.
#   2. Run scripts/gen-symbols.sh on kernel-stage1.elf to emit the real
#      symbol_data.c.
#   3. Compile symbol_data.c and re-link kernel.elf with the real table.
#
# Function addresses are stable across the two passes because the symbol
# table lives in .rodata, which sits AFTER .text in linker.ld; growing it
# does not shift any code.
# ----------------------------------------------------------------------------

$(KERNEL_ELF): $(ALL_OBJECTS) $(SYMBOLS_OBJ)
	@echo "Linking kernel (pass 2 - with symbol table)..."
	$(LD) $(LDFLAGS) $(ALL_OBJECTS) $(SYMBOLS_OBJ) -o $@
	@echo "Kernel linked successfully: $@"

$(KERNEL_STAGE1): $(ALL_OBJECTS) $(SYMBOLS_STUB_O)
	@echo "Linking kernel (pass 1 - empty symbol table)..."
	$(LD) $(LDFLAGS) $(ALL_OBJECTS) $(SYMBOLS_STUB_O) -o $@

$(SYMBOLS_C): $(KERNEL_STAGE1) scripts/gen-symbols.sh
	@echo "Generating symbol table from $<..."
	@bash scripts/gen-symbols.sh $< $@

$(SYMBOLS_STUB_C):
	@mkdir -p $(dir $@)
	@printf '/* Auto-generated stub used only for the stage-1 link. */\n#include <aeos/symbols.h>\nconst symbol_entry_t aeos_symbols[1] = { { 0, "" } };\nconst uint32_t aeos_symbols_count = 0u;\n' > $@

$(SYMBOLS_OBJ): $(SYMBOLS_C)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(SYMBOLS_STUB_O): $(SYMBOLS_STUB_C)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

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
	@echo "Click in window to grab mouse, Ctrl+Alt+G to release"
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

# ----------------------------------------------------------------------------
# Test runner
#
# `make test` rebuilds the kernel with TEST=1 (which links src/kernel/test_runner.c
# instead of main.c), boots it under QEMU with semihosting, captures stdout
# to build/test.log, and greps the test runner's "TEST RESULTS: P PASSED, F
# FAILED" line. Exit code is 0 only when the runner reports zero failures.
# ----------------------------------------------------------------------------
test:
	@echo "Building test kernel..."
	@$(MAKE) --no-print-directory clean >/dev/null
	@$(MAKE) --no-print-directory TEST=1 >/dev/null
	@mkdir -p $(BUILD_DIR)
	@echo "Running test kernel under QEMU..."
	@timeout 30 qemu-system-aarch64 -M virt -cpu cortex-a57 -m 256M \
		-nographic -kernel $(KERNEL_ELF) \
		-semihosting-config enable=on,target=native \
		> $(BUILD_DIR)/test.log 2>&1 || true
	@echo "----- test output -----"
	@cat $(BUILD_DIR)/test.log
	@echo "----- summary -----"
	@if grep -q 'TEST RESULTS:' $(BUILD_DIR)/test.log; then \
		PASS=$$(grep -oE '[0-9]+ PASSED' $(BUILD_DIR)/test.log | grep -oE '[0-9]+'); \
		FAIL=$$(grep -oE '[0-9]+ FAILED' $(BUILD_DIR)/test.log | grep -oE '[0-9]+'); \
		echo "Passed: $$PASS"; \
		echo "Failed: $$FAIL"; \
		if [ "$$FAIL" = "0" ]; then \
			echo "OK"; exit 0; \
		else \
			echo "FAIL"; exit 1; \
		fi; \
	else \
		echo "Test runner did not finish (no TEST RESULTS line)"; \
		exit 1; \
	fi

# ----------------------------------------------------------------------------
# Security audit gate
#
# `make audit` is the phase gate for the 13.B security work. It runs the full
# test suite (which now includes the security smoke scenarios in test_runner.c)
# and then asserts that every expected sec_* scenario reported PASS in
# build/test.log. The second check is what makes audit distinct from test: if a
# future change silently drops a security scenario, the suite could still report
# all-pass while the security coverage quietly shrank. Asserting the named PASS
# lines turns that into a hard failure. Exits non-zero on any test failure or
# any missing sec_* scenario.
# ----------------------------------------------------------------------------
SEC_SCENARIOS = sec_kcalloc_overflow \
                sec_stack_guard \
                sec_double_free_after_merge \
                sec_vfs_path_too_long \
                sec_editor_growth_overflow

audit: test
	@echo "----- security scenario check -----"
	@missing=0; \
	for s in $(SEC_SCENARIOS); do \
		if grep -q "PASS: $$s" $(BUILD_DIR)/test.log; then \
			echo "present: $$s"; \
		else \
			echo "MISSING or not PASS: $$s"; \
			missing=1; \
		fi; \
	done; \
	if [ "$$missing" = "0" ]; then \
		echo "All security scenarios present and green."; \
		echo "OK"; exit 0; \
	else \
		echo "Security audit failed: a scenario is missing or did not pass."; \
		exit 1; \
	fi

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
