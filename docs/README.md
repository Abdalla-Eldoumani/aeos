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
6. **VirtIO GPU**: Graphics via VirtIO GPU MMIO device with legacy (v1) protocol

## Build Environment

- **Platform**: WSL or Linux with ARM cross-compilation tools
- **Compiler**: `aarch64-linux-gnu-gcc`
- **Assembler**: `aarch64-linux-gnu-as`
- **Preprocessor**: `m4` (for assembly macros)
- **Emulator**: QEMU `qemu-system-aarch64`
