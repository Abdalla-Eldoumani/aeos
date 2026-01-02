# AEOS - Abdalla's Educational Operating System

A 64-bit ARM kernel for learning operating system fundamentals, built from scratch for the ARMv8-A architecture.

## Overview

AEOS is a bare-metal operating system kernel that runs on QEMU's ARM virt machine. It demonstrates core OS concepts including memory management, process scheduling, system calls, and filesystem implementation.

## Features

- **Bootstrap**: EL2 => EL1 privilege level transition
- **Memory Management**: Buddy allocator (physical) and first-fit heap allocator
- **Process Management**: Cooperative round-robin scheduler with context switching
- **System Calls**: Direct function call interface (exit, write, read, getpid, yield)
- **Filesystem**: VFS abstraction layer with in-memory ramfs implementation
- **Interrupt Handling**: Exception vectors and handlers (GIC/timer support present but not initialized)
- **Interactive Shell**: Command-line interface with 10 built-in commands
- **Graphics**: Framebuffer support with ASCII art preview (hardware display not configured)

## System Requirements

### Development Environment
- **WSL** (Windows Subsystem for Linux) or Linux system with ARM64 toolchain
- ARM64 cross-compiler: `aarch64-linux-gnu-gcc`
- GNU `make`
- `m4` macro processor
- QEMU: `qemu-system-aarch64`

### Tested Configuration
- Developed and tested on WSL
- Should work on any system with ARM64 development tools

## Building

```bash
make          # Build kernel
make clean    # Remove build artifacts
make dump     # Disassemble kernel
```

## Running

```bash
make run             # Text mode with ASCII art preview
make run-ramfb       # SDL window (requires ramfb configuration)
make run-vnc         # VNC server on port 5900
make run-virtio      # VirtIO GPU mode
make debug           # GDB server mode (connect on port 1234)
```

### Debug Mode
```bash
DEBUG=1 make run     # Enable debug logging
```

## Shell Commands

Available commands in the interactive shell:
- `help` - Show available commands
- `clear` - Clear screen
- `echo <text>` - Print text
- `ps` - List processes
- `meminfo` - Show memory statistics
- `ls [path]` - List directory contents
- `cat <file>` - Display file contents
- `touch <file>` - Create empty file
- `uname` - Show system information
- `exit` - Halt system

Note: File operations (`touch`, `cat`, `mkdir`) are implemented but filesystem is in-memory only. Changes are lost when QEMU exits.

## Architecture Details

### Platform
- **CPU**: ARM Cortex-A57 (ARMv8-A)
- **Memory**: 256MB RAM at 0x40000000
- **Kernel Heap**: 4MB
- **Stack**: 128KB

### Memory Map
```
0x40000000  Kernel start
0x40217000  Heap start
0x40617000  Heap end / PMM start
0x50000000  RAM end (256MB total)
```

### Hardware (QEMU virt)
```
0x08000000  GIC Distributor
0x08010000  GIC CPU Interface
0x09000000  UART0 (PL011)
0x0a000000  VirtIO devices
```

## Known Limitations

### Not Implemented/Configured
- **GIC and Timer**: Code present but initialization disabled
- **Preemptive Multitasking**: Currently cooperative scheduling only
- **User Space (EL0)**: All code runs at EL1 (kernel mode)
- **MMU/Paging**: Virtual memory not configured
- **Filesystem Persistence**: In-memory only, no disk backing
- **Hardware Graphics**: Framebuffer renders to memory only

### Interrupt Handling
Exception vectors and GIC driver code exist but are not activated. The system currently operates without hardware interrupts or timer-based scheduling.

## Project Structure

```
aeos/
├── src/
│   ├── boot/          # Bootstrap assembly (EL2→EL1 transition)
│   ├── kernel/        # Core kernel (main, kprintf, shell)
│   ├── drivers/       # Hardware drivers (UART, DTB, graphics)
│   ├── mm/            # Memory management (PMM, heap)
│   ├── interrupts/    # Exception handling (vectors, GIC, timer)
│   ├── proc/          # Process management (scheduler, context switch)
│   ├── syscall/       # System call dispatcher
│   ├── fs/            # Filesystem (VFS, ramfs)
│   └── lib/           # Utility functions (string operations)
├── include/           # Header files
│   ├── aeos/          # Kernel headers
│   └── asm/           # ARM system register definitions
├── Makefile           # Build system
└── linker.ld          # Linker script
```

## Development Status

AEOS is a work-in-progress educational project. The project was developed in seven planned phases:
1. Boot & Basic Output
2. Memory Management
3. Interrupts & Exceptions (code present, not activated)
4. Process Management
5. System Calls
6. Virtual Filesystem
7. Interactive Shell

The kernel successfully boots, initializes all subsystems, and provides an interactive shell. Future work may include enabling interrupts, implementing user space, and adding MMU support.

## License

MIT License. See `LICENSE` file for details.