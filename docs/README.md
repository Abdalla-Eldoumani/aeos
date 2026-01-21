# AEOS Documentation

Technical documentation for AEOS kernel implementation.

## Overview

This documentation covers the implementation details of each kernel subsystem. Each section includes an overview and detailed code walkthroughs.

## Sections

### 1. [Basic Output](./01-basic-output/)
Bootstrap code and console output implementation.

- **Location**: `src/boot/`, `src/drivers/uart.c`, `src/kernel/kprintf.c`
- **What it does**: Initializes the processor, sets up the execution environment, and provides formatted console output through the PL011 UART
- **Key concepts**: Exception levels, stack setup, BSS initialization, UART driver, printf implementation

### 2. [Memory Management](./02-memory-management/)
Physical and virtual memory allocation.

- **Location**: `src/mm/`
- **What it does**: Manages physical memory using a buddy allocator and provides kernel heap allocation with a first-fit allocator
- **Key concepts**: Buddy algorithm, heap allocation, page management, memory statistics

### 3. [Interrupts and Exceptions](./03-interrupts-exceptions/)
Interrupt handling and exception management.

- **Location**: `src/interrupts/`
- **What it does**: Handles exceptions and hardware interrupts using the ARM GICv2 and generic timer
- **Key concepts**: Exception vectors, GIC configuration, timer interrupts, FIQ handling
- **Status**: Fully functional with FIQ-based timer interrupts at 100 Hz

### 4. [Process Management](./04-process-management/)
Process creation and scheduling.

- **Location**: `src/proc/`
- **What it does**: Creates and manages processes using preemptive round-robin scheduling with context switching
- **Key concepts**: Process Control Block, context switching, preemptive scheduling, process states

### 5. [System Calls](./05-system-calls/)
System call interface and implementation.

- **Location**: `src/syscall/`
- **What it does**: Provides system call dispatcher and implementations for exit, write, read, getpid, and yield
- **Key concepts**: System call table, argument passing, direct function calls
- **Note**: Uses direct function calls instead of SVC exceptions (appropriate for EL1 kernel threads)

### 6. [Virtual Filesystem](./06-virtual-filesystem/)
File system abstraction and ramfs implementation.

- **Location**: `src/fs/`
- **What it does**: Implements VFS layer with path resolution, ramfs in-memory filesystem, and host persistence via semihosting
- **Key concepts**: VFS abstraction, inodes, file descriptors, directory operations, semihosting persistence

### 7. [Interactive Shell](./07-interactive-shell/)
Command-line interface and text editor.

- **Location**: `src/kernel/shell.c`, `src/kernel/editor.c`
- **What it does**: Provides interactive shell with 24 built-in commands, colorized output, and a vim-like text editor
- **Key concepts**: Command parsing, line editing, file operations, modal text editing

### 8. [Graphics and GUI](./08-graphics-gui/)
Graphical desktop environment implementation.

- **Location**: `src/kernel/bootscreen.c`, `src/kernel/gui.c`, `src/kernel/wm.c`, `src/kernel/window.c`, `src/kernel/desktop.c`, `src/kernel/event.c`
- **What it does**: Provides a complete graphical desktop environment with boot screen, window manager, desktop icons, and taskbar
- **Key concepts**: Framebuffer graphics, window compositing, event-driven architecture, mouse cursor rendering

#### Key Components:
- **Boot Screen** (`bootscreen.c`): Animated progress bar with stage messages
- **Event System** (`event.c`): Circular queue for keyboard/mouse events
- **Window Manager** (`wm.c`): Z-ordered window list, focus tracking, window dragging
- **Window** (`window.c`): Window creation, decorations, client area management
- **Desktop** (`desktop.c`): Background, icons, taskbar, start menu
- **GUI Init** (`gui.c`): Coordinates initialization of all GUI subsystems

### 9. [VirtIO Drivers](./09-virtio-drivers/)
VirtIO device drivers for graphics and input.

- **Location**: `src/drivers/virtio_gpu.c`, `src/drivers/virtio_input.c`, `src/drivers/framebuffer.c`
- **What it does**: Interfaces with QEMU's VirtIO devices for GPU display and mouse/keyboard input
- **Key concepts**: VirtIO MMIO, virtqueues, legacy (v1) protocol, event polling

#### Key Components:
- **VirtIO GPU** (`virtio_gpu.c`): Creates display resources, transfers framebuffer to host
- **VirtIO Input** (`virtio_input.c`): Polls keyboard and mouse devices for events
- **Framebuffer** (`framebuffer.c`): Graphics primitives (putpixel, fill_rect, draw_line, putchar, puts)

### 10. [GUI Applications](./10-gui-applications/)
Built-in graphical applications.

- **Location**: `src/apps/`
- **What it does**: Provides user-facing applications for the desktop environment
- **Applications**:
  - **Terminal** (`terminal.c`): GUI terminal emulator with shell integration
  - **File Manager** (`filemanager.c`): Graphical file browser
  - **Settings** (`settings.c`): System information and memory statistics
  - **About** (`about.c`): System information dialog

## Documentation Structure

Each section directory contains:
- `README.md` - Overview and implementation summary
- `implementation.md` - Detailed code walkthrough

## Design Decisions

1. **Preemptive Scheduling**: Round-robin scheduling with 100 Hz timer tick for time slicing
2. **FIQ-Based Timer**: Timer interrupts route as FIQ on QEMU virt; handled via direct timer status checking
3. **Direct System Calls**: System calls use function calls instead of SVC exceptions
4. **Semihosting Persistence**: Filesystem persists to host via ARM semihosting
5. **Kernel Mode Only**: All code runs at EL1, no user space (EL0)
6. **VirtIO Legacy Mode**: GPU and input use VirtIO MMIO with legacy (v1) protocol
7. **Event-Driven GUI**: Mouse/keyboard events queued and processed in main loop
8. **Compositing Window Manager**: Windows have backbuffers, composited to main framebuffer

## Build Environment

- **Platform**: WSL or Linux with ARM cross-compilation tools
- **Compiler**: `aarch64-linux-gnu-gcc`
- **Assembler**: `aarch64-linux-gnu-as`
- **Preprocessor**: `m4` (for assembly macros)
- **Emulator**: QEMU `qemu-system-aarch64`

## Running Modes

```bash
make run           # Text mode (UART shell only)
make run-ramfb     # Graphical desktop (recommended)
make run-virtio    # Graphical desktop (alternative)
DEBUG=1 make run-ramfb  # Debug mode with logging
```
