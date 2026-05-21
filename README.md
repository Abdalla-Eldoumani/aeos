# AEOS - Abdalla's Educational Operating System

A 64-bit ARM kernel with a graphical desktop environment, built from scratch for learning operating system fundamentals on the ARMv8-A architecture.

## Overview

AEOS is a bare-metal AArch64 kernel that runs in QEMU's `virt` machine. It boots from EL2 to EL1, brings up its own buddy allocator and first-fit heap, services GICv2 interrupts and the ARM Generic Timer, mounts an in-RAM filesystem with semihosting persistence, and renders a windowed desktop with eight built-in apps over a VirtIO GPU.

### Scope

AEOS is **deliberately small**. It is meant to be readable end-to-end in a few sittings, not to be a Unix clone. To keep that promise, several things you might expect from a "real" OS are intentionally absent:

- **No MMU.** All addresses are physical. There is no virtual memory, no page tables, no copy-on-write, no `mmap`.
- **No userspace.** Everything runs at EL1. There are no EL0 processes, no per-process address spaces, no privilege boundary between the shell and the kernel. System calls are direct C function calls, not `SVC` traps.
- **No SMP.** Secondary CPUs are parked at `_start`; only CPU 0 ever runs.
- **No networking.** There is no IP stack, no socket layer, no network driver.
- **No real clock.** Timestamps are 0 because there is no RTC driver.

If you want any of those, AEOS is not the right starting point. If you want a small, hackable system you can read top-to-bottom and modify in an afternoon, it is.

## Features

### Graphical Desktop Environment
- **Boot Screen**: 8x16 wordmark, slim progress bar, cross-faded stage messages, fade-out to desktop
- **Desktop**: Gradient background, clickable icons, taskbar with start menu, window buttons, system clock
- **Window Manager**: Overlapping windows with title bars, close buttons, drag clamping, focused-window drop shadow, slide+fade open animation, fade-out close animation, 30 FPS compositing
- **Notifications**: Three-slot toast stack in the top-right; 220 ms slide-in, 4 s visible, 240 ms fade-out, info / warn / error stripe colors
- **Keyboard Shortcuts**: Alt+Tab cycles focus through windows in z-order, Alt+F4 closes the focused window, Esc dismisses overlays
- **Mouse Cursor**: Software cursor with backup/restore compositing

### Applications
- **Terminal**: 78x22 GUI terminal emulator (8x16 cells) with ANSI parser and 200-line scrollback
- **File Manager**: Browse the filesystem graphically; toast errors on VFS failures
- **Settings**: View system information and memory usage
- **About**: System information dialog
- **Calculator**: Standard four-function calculator with int64 fixed-point arithmetic
- **System Monitor**: Live 60-second heap-usage graph
- **Notes**: Flat (non-modal) text editor that wraps the editor buffer engine and saves to `/notes.txt`
- **Tetris**: 10x20 board with all seven tetrominoes, gravity that speeds up by level, soft/hard drop, hold-to-pause, and a high score persisted to `/tetris_high.bin`

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

### Installing the toolchain

On Debian, Ubuntu, or WSL Ubuntu:

```bash
sudo apt update
sudo apt install -y gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
                    qemu-system-arm m4 make
```

On Fedora:

```bash
sudo dnf install -y gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
                    qemu-system-aarch64 m4 make
```

On macOS (Homebrew):

```bash
brew install aarch64-elf-gcc qemu m4 make
# then build with: make CROSS_COMPILE=aarch64-elf-
```

The Makefile defaults to the `aarch64-linux-gnu-` prefix; override `CROSS_COMPILE` if your toolchain uses a different one.

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
2. **Desktop** - Eight application icons:
   - Terminal (green) - Opens terminal emulator
   - Files (yellow) - Opens file manager
   - Settings (blue) - Opens system settings
   - About (purple) - Opens about dialog
   - Calc (purple) - Opens calculator
   - SysMon (green) - Opens system monitor
   - Notes (yellow) - Opens notes editor
   - Tetris (red) - Opens the Tetris game
3. **Taskbar** - Start button, window buttons, clock

### Mouse Controls
- **Move**: Move mouse to move cursor
- **Single-click**: Select desktop icon
- **Double-click**: Launch application
- **Drag**: Click and drag window title bar to move
- **Close**: Click red X button on window

### Keyboard Shortcuts
- **Alt + Tab**: Cycle focus through visible windows in z-order. Hold Alt and press Tab repeatedly to keep stepping; releasing Alt commits the selection and raises the focused window to the top.
- **Alt + F4**: Close the focused window. Same fade-out path as the window's close button — the app's `on_close` runs after the close animation completes.
- **Esc**: Dismiss UI overlays without sending the key to the focused app. Closes the start menu and starts the fade-out for any active toast notifications. If nothing's open to dismiss, Esc falls through to the focused window like any other key.

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

## Testing

`make test` builds the kernel with `TEST=1`, which links a self-contained test runner (`src/kernel/test_runner.c`) as `kernel_main` instead of the normal entry point. The runner brings up memory, the VFS, and the process subsystem, then exercises PMM, heap, VFS, process, shell-parse, symbol-lookup, framebuffer, and security smoke scenarios, 20 in total. The five security scenarios cover the 13.B audit invariants: kcalloc overflow rejection, stack-guard sentinel detection, double-free-after-merge refusal, VFS path-length rejection, and editor growth-overflow refusal. Each scenario logs `PASS: <name>` or `FAIL: <name> (<why>)`. After the suite finishes the runner prints `TEST RESULTS: P PASSED, F FAILED` and exits via semihosting; the `test` target captures stdout to `build/test.log`, parses that line, and exits 0 only when the failure count is zero. Run takes well under a second.

`make audit` runs the same suite and additionally asserts that every security scenario reported `PASS`, so a future change that drops one fails the gate. It exits non-zero on any test failure or any missing security scenario.

GitHub Actions runs `make`, `make TEST=1`, `make test`, and a TODO/FIXME guard on every push and pull request to `master` (see `.github/workflows/ci.yml`).

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
0x40229000  Heap start (4 MB; address moves with kernel size)
0x40629000  Heap end
0x40649000  Stack top (128 KB above heap end)
0x50000000  RAM end (256 MB total; PMM owns everything past stack top)
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
│   │   ├── notify.c   # Toast notification engine
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
│   │   ├── about.c    # About dialog
│   │   ├── calculator.c # Four-function calculator
│   │   ├── sysmon.c   # Live heap usage graph
│   │   ├── notes.c    # GUI text editor
│   │   └── tetris.c   # Tetris game
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
