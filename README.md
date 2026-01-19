# AEOS - Abdalla's Educational Operating System

A 64-bit ARM kernel for learning operating system fundamentals, built from scratch for the ARMv8-A architecture.

## Overview

AEOS is a bare-metal operating system kernel that runs on QEMU's ARM virt machine. It demonstrates core OS concepts including memory management, process scheduling, system calls, filesystem implementation, and interactive shell design.

## Features

- **Bootstrap**: EL2 to EL1 privilege level transition with stack and BSS setup
- **Memory Management**: Buddy allocator for physical memory, first-fit heap allocator
- **Process Management**: Preemptive round-robin scheduler with context switching
- **Interrupts**: ARM GICv2 + Generic Timer via FIQ handling (100 Hz tick)
- **System Calls**: Direct function call interface (exit, write, read, getpid, yield)
- **Filesystem**: VFS abstraction layer with ramfs implementation and host persistence
- **Text Editor**: Vim-like modal editor with insert/normal/ex modes
- **Interactive Shell**: 24 built-in commands with colorized output
- **Graphics**: VirtIO GPU driver with framebuffer display
- **Persistence**: Filesystem saves to host via ARM semihosting

## System Requirements

### Development Environment
- **WSL** (Windows Subsystem for Linux) or Linux with ARM64 toolchain
- ARM64 cross-compiler: `aarch64-linux-gnu-gcc`
- GNU `make`
- `m4` macro processor
- QEMU: `qemu-system-aarch64`

## Building

```bash
make          # Build kernel
make clean    # Remove build artifacts
make dump     # Disassemble kernel
```

## Running

```bash
make run             # Text mode with persistence (recommended)
make run-virtio      # SDL window with VirtIO GPU
make run-ramfb       # SDL window with ramfb device
make run-vnc         # VNC server on port 5900
make debug           # GDB server on port 1234
```

Debug mode:
```bash
DEBUG=1 make run     # Enable debug logging
```

Exit QEMU: Press `Ctrl+A` then `X`

Graphical mode shows a test pattern to verify the VirtIO GPU driver is working.

## Shell Commands

| Command | Description |
|---------|-------------|
| help | Show available commands |
| clear | Clear screen |
| echo | Print text to console |
| ls | List directory contents |
| cat | Display file contents |
| touch | Create empty file |
| mkdir | Create directory |
| rm | Remove file or directory |
| cp | Copy file |
| mv | Move/rename file |
| cd | Change directory |
| pwd | Print working directory |
| write | Write text to file |
| hexdump | Hex dump of file |
| grep | Search for pattern in file |
| edit / vi | Vim-like text editor |
| ps | List processes |
| meminfo | Memory statistics |
| uptime | System uptime |
| irqinfo | Interrupt statistics |
| history | Command history |
| time | Time command execution |
| uname | System information |
| save | Save filesystem to host |
| exit | Halt system |

## Text Editor

The `edit` and `vi` commands open a vim-like text editor:

**Normal Mode**:
- `h/j/k/l` or arrows: Move cursor
- `i`: Enter insert mode
- `x`: Delete character
- `dd`: Delete line
- `0/$`: Start/end of line
- `gg/G`: First/last line

**Insert Mode**:
- Type to insert text
- `Esc`: Return to normal mode

**Ex Mode** (press `:`):
- `:w` - Save file
- `:q` - Quit
- `:wq` - Save and quit
- `:q!` - Quit without saving

## Filesystem Persistence

Files are stored in RAM during runtime. Use the `save` command to persist the filesystem to the host machine. The filesystem is saved to `aeos_fs.img` and automatically loaded on next boot.

```
AEOS> touch myfile.txt
AEOS> write myfile.txt Hello World
AEOS> save
Filesystem saved successfully!
```

## Architecture

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

## Project Structure

```
aeos/
├── src/
│   ├── boot/          # Bootstrap assembly
│   ├── kernel/        # Core kernel (main, kprintf, shell, editor)
│   ├── drivers/       # Hardware drivers (UART, semihosting)
│   ├── mm/            # Memory management (PMM, heap)
│   ├── interrupts/    # Exception handling (vectors, GIC, timer)
│   ├── proc/          # Process management (scheduler, context)
│   ├── syscall/       # System call dispatcher
│   ├── fs/            # Filesystem (VFS, ramfs, persistence)
│   └── lib/           # Utility functions
├── include/           # Header files
├── docs/              # Implementation documentation
├── Makefile           # Build system
└── linker.ld          # Linker script
```

## Known Limitations

- **Privilege Level**: All code runs at EL1 (no user space)
- **Virtual Memory**: MMU not configured
- **Shell Input**: Arrow keys not functional (escape sequences disabled)

## Documentation

Detailed implementation documentation is available in `docs/`. Each section covers a specific kernel subsystem with code walkthroughs and API references.

## License

MIT License. See `LICENSE` file for details.
