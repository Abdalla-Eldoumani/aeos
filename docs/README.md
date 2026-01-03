# AEOS Documentation

Technical documentation for AEOS kernel implementation.

## Overview

This documentation covers the implementation details of each kernel subsystem. AEOS was built in seven sequential sections, each adding core operating system functionality. While originally developed in phases, the documentation is organized by functional area to provide clear reference material.

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
- **Key concepts**: Exception vectors, GIC configuration, timer interrupts, exception handling
- **Status**: Code present but not activated in current configuration

### 4. [Process Management](./04-process-management/)
Process creation and scheduling.

- **Location**: `src/proc/`
- **What it does**: Creates and manages processes using cooperative round-robin scheduling with context switching
- **Key concepts**: Process Control Block, context switching, cooperative scheduling, process states

### 5. [System Calls](./05-system-calls/)
System call interface and implementation.

- **Location**: `src/syscall/`
- **What it does**: Provides system call dispatcher and implementations for exit, write, read, getpid, and yield
- **Key concepts**: System call table, argument passing, direct function calls
- **Note**: Uses direct function calls instead of SVC exceptions (appropriate for EL1 kernel threads)

### 6. [Virtual Filesystem](./06-virtual-filesystem/)
File system abstraction and ramfs implementation.

- **Location**: `src/fs/`
- **What it does**: Implements VFS layer with path resolution and ramfs in-memory filesystem
- **Key concepts**: VFS abstraction, inodes, file descriptors, directory operations, ramfs
- **Limitation**: In-memory only, no disk persistence

### 7. [Interactive Shell](./07-interactive-shell/)
Command-line interface for user interaction.

- **Location**: `src/kernel/shell.c`
- **What it does**: Provides interactive shell with command parsing and 17 built-in commands
- **Key concepts**: Command parsing, line editing, file operations, system commands

## Documentation Structure

Each section directory contains:
- `README.md` - Overview and implementation summary
- `implementation.md` - Detailed code walkthrough

## Implementation Notes

### Design Decisions

1. **Cooperative Scheduling**: Round-robin scheduling requires processes to explicitly yield
2. **Direct System Calls**: System calls use function calls instead of SVC exceptions
3. **In-Memory Filesystem**: Ramfs stores everything in RAM with no disk backing
4. **Disabled Interrupts**: GIC and timer code exists but is not initialized
5. **Kernel Mode Only**: All code runs at EL1, no user space (EL0)

### Known Limitations

- No MMU or virtual memory
- No preemptive multitasking
- No filesystem persistence
- No hardware display output
- Interrupts not enabled

### Build Environment

- **Platform**: WSL (Windows Subsystem for Linux) or Linux with ARM cross-compilation tools
- **Compiler**: `aarch64-linux-gnu-gcc`
- **Assembler**: `aarch64-linux-gnu-as`
- **Preprocessor**: `m4` (for assembly macros)
- **Emulator**: QEMU `qemu-system-aarch64`

## Using This Documentation

1. Start with each section's README to understand the high-level architecture
2. Read `implementation.md` for detailed code walkthroughs

Each section is self-contained and can be read independently, though they build on each other conceptually.