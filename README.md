# AEOS - Abdalla's Educational Operating System

A 64-bit ARM kernel with a graphical desktop environment, built from scratch for learning operating system fundamentals on the ARMv8-A architecture.

## Overview

AEOS is a bare-metal operating system kernel that runs on QEMU's ARM virt machine. It features a full graphical desktop environment with mouse support, draggable windows, and multiple applications. The project demonstrates core OS concepts including memory management, process scheduling, interrupt handling, filesystem implementation, device drivers, and GUI programming.

## Features

### Graphical Desktop Environment
- **Boot Screen**: Animated boot progress with logo, progress bar, and stage messages
- **Desktop**: Gradient background with clickable application icons
- **Window Manager**: Overlapping windows with title bars, close buttons, and drag support
- **Taskbar**: Start menu button, window buttons for running apps, and system clock
- **Mouse Cursor**: Hardware-accelerated cursor with proper compositing

### Applications
- **Terminal**: GUI terminal emulator with shell access
- **File Manager**: Browse the filesystem graphically
- **Settings**: View system information and memory usage
- **About**: System information dialog

### Core OS Features
- **Bootstrap**: EL2 to EL1 privilege level transition with stack and BSS setup
- **Memory Management**: Buddy allocator for physical memory, first-fit heap allocator
- **Process Management**: Preemptive round-robin scheduler with context switching (100 Hz)
- **Interrupts**: ARM GICv2 + Generic Timer via FIQ handling
- **System Calls**: Direct function call interface (exit, write, read, getpid, yield)
- **Filesystem**: VFS abstraction layer with ramfs implementation and host persistence
- **Text Editor**: Vim-like modal editor with insert/normal/ex modes
- **Interactive Shell**: 24 built-in commands with colorized output

### Device Drivers
- **VirtIO GPU**: Framebuffer graphics (640x480 @ 32bpp)
- **VirtIO Input**: Mouse and keyboard support via VirtIO MMIO
- **PL011 UART**: Serial console for text mode
- **ARM Semihosting**: Filesystem persistence to host

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

### Graphical Mode (Recommended)
```bash
make run-ramfb       # Desktop environment with mouse/keyboard
make run-virtio      # Alternative GPU driver
```

### Text Mode
```bash
make run             # Text-only shell via UART
```

### Debug Mode
```bash
DEBUG=1 make run-ramfb   # Graphical mode with debug logging
make debug               # GDB server on port 1234
make run-vnc             # VNC server on port 5900
```

Exit QEMU: Press `Ctrl+A` then `X`

## Desktop Environment

When running in graphical mode (`make run-ramfb`), AEOS displays:

1. **Boot Screen** - Progress bar showing initialization stages
2. **Desktop** - Four application icons:
   - Terminal (green) - Opens terminal emulator
   - Files (yellow) - Opens file manager
   - Settings (blue) - Opens system settings
   - About (purple) - Opens about dialog
3. **Taskbar** - Start button, window buttons, clock

### Mouse Controls
- **Move**: Move mouse to move cursor
- **Single-click**: Select desktop icon
- **Double-click**: Launch application
- **Drag**: Click and drag window title bar to move
- **Close**: Click red X button on window

## Shell Commands

Available in text mode or via the Terminal application:

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
- **Framebuffer**: 640x480 @ 32bpp (~1.2MB)

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
0x0a000000  VirtIO devices (GPU, keyboard, mouse)
```

## Project Structure

```
aeos/
├── src/
│   ├── boot/          # Bootstrap assembly
│   ├── kernel/        # Core kernel, shell, editor, GUI subsystem
│   │   ├── main.c     # Kernel entry point
│   │   ├── shell.c    # Text-mode shell
│   │   ├── editor.c   # Vim-like editor
│   │   ├── bootscreen.c # Boot progress screen
│   │   ├── event.c    # Event queue system
│   │   ├── window.c   # Window management
│   │   ├── wm.c       # Window manager
│   │   ├── desktop.c  # Desktop environment
│   │   └── gui.c      # GUI initialization
│   ├── drivers/       # Hardware drivers
│   │   ├── uart.c     # Serial console
│   │   ├── framebuffer.c # Graphics primitives
│   │   ├── virtio_gpu.c  # VirtIO GPU driver
│   │   ├── virtio_input.c # Mouse/keyboard driver
│   │   └── semihosting.c # Host I/O
│   ├── apps/          # GUI applications
│   │   ├── terminal.c # Terminal emulator
│   │   ├── filemanager.c # File browser
│   │   ├── settings.c # System settings
│   │   └── about.c    # About dialog
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
- **Shell Input**: Arrow keys not functional in text mode (escape sequences disabled)
- **GUI Applications**: Some app functionality is basic/placeholder

## Documentation

Detailed implementation documentation is available in `docs/`. Each section covers a specific kernel subsystem with code walkthroughs and API references.

## License

MIT License. See `LICENSE` file for details.
