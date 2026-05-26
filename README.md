# AEOS - Abdalla's Educational Operating System

A 64-bit ARM kernel with a graphical desktop environment, built from scratch for learning operating system fundamentals on the ARMv8-A architecture.

## Overview

AEOS is a bare-metal AArch64 kernel that runs in QEMU's `virt` machine. It boots from EL2 to EL1, brings up its own buddy allocator and first-fit heap, services GICv2 interrupts and the ARM Generic Timer, mounts an in-RAM filesystem with semihosting persistence, and renders a windowed desktop with eight built-in apps over a VirtIO GPU.

### Scope

AEOS is **deliberately small**. It is meant to be readable end-to-end in a few sittings, not to be a Unix clone. It now carries the foundations you would expect from a "real" OS, each in a bounded form chosen to stay readable:

- **MMU on.** `src/mm/vmm.c` builds an identity map (RAM as a Normal-WB cacheable 1 GB block, the low MMIO window as a Device-nGnRnE block) plus a TTBR1 high-half alias of RAM, and enables the MMU and caches. The kernel runs identity-mapped, so virtual equals physical for kernel addresses.
- **An EL0/EL1 privilege boundary.** A static ELF64 loaded from a file runs at EL0 and reaches the kernel only through `svc #0` traps decoded by the EL1 vector table. A privileged instruction from EL0 faults to EL1. The loaded program is mapped with per-segment W^X (executable segments are read-only at EL0).
- **SMP.** `smp_init` brings up secondary cores 1 through 3 via PSCI CPU_ON with a bounded handshake; `ps` shows a CPU column and the boot serial reports four cores online.
- **Networking.** A minimal in-kernel stack (Ethernet, ARP, IPv4, ICMP echo) over a poll-driven virtio-net driver answers ARP for its own address and the `ping` command gets an ICMP echo reply from the QEMU slirp gateway.
- **A wall clock.** A PL031 RTC driver reads the host wall-clock seconds and drives the taskbar clock and the boot log. `run-ramfb` runs QEMU with `-rtc base=localtime`, so the taskbar shows local time (the host timezone, with DST), not UTC.

The remaining gaps are deliberate, and AEOS stays honest about them:

- **No kernel W^X.** The kernel's code, rodata, data, heap, and stack share one RWX 1 GB block. Per-segment W^X exists only for loaded EL0 code, not the kernel image.
- **No cross-core preemptive scheduling.** Cores come online and park in `wfe`; the round-robin scheduler runs on the primary. Interactive kill of a running EL0 program from a second prompt needs the preemption work that is the next milestone.
- **No TCP, UDP, DHCP, or DNS.** The stack is ARP plus ICMP echo only, slirp-only, with no socket layer.
- **Host-trusting persistence.** The filesystem and shell history persist through ARM semihosting, which trusts the host.

If you want a production OS, AEOS is not it. If you want a small, hackable system you can read top-to-bottom and modify in an afternoon, it is.

## What's interesting

A few parts of AEOS are worth a reader's attention, either because of how they
work under the constraints above or because they are not what you would expect
from a kernel this small.

- **A compositing window manager with no floating point.** The desktop runs
  overlapping windows with slide-and-fade open animations, a fade-out close, and
  toast notifications composited on top, all at 30 FPS. Because the kernel is
  built with `-mgeneral-regs-only` (CPACR_EL1 never enables Q-register access at
  EL1, so any float or SIMD code traps), every easing curve is computed in Q0.8
  fixed-point in `src/lib/anim.c`. The animation system that would normally lean
  on floats does the same work in integers.

- **Two ways to make a syscall, one dispatcher.** The kernel runs at EL1, where a
  "system call" is a direct C call: `syscall(num, ...)` in `src/syscall/syscall.c`
  is a table lookup, not an `SVC` trap, and the GUI and shell still use it. An EL0
  program, by contrast, issues `svc #0`, which traps into the EL1 vector table and
  is routed through the SAME `syscall_handler`. The EL0 path is additive; it did
  not replace the direct-call path. Watching one dispatcher serve both a direct
  call and a trapped `svc` is a compact way to see what a privilege boundary buys.

- **Security invariants you can re-check with one command.** A hardening pass
  added defenses that `make audit` verifies on every run: a stack-guard sentinel
  catches kernel stack overflow (the kernel runs from one coarse RWX block with no
  faulting guard page of its own, so the boot stack carries a magic value that the
  exception path and the timer tick both check), heap block headers carry a magic
  value re-stamped on every header operation so `kfree` refuses a caller pointer
  whose magic is wrong, the editor and VFS bound their attacker-influenced sizes
  and path lengths before allocating, and the panic path masks DAIF on entry so a
  timer interrupt cannot corrupt the crash dump. `make audit` runs the test suite
  and then asserts each security scenario actually reported `PASS`, so silently
  dropping one fails the build.

- **The filesystem survives a reboot through semihosting.** Files live in RAM at
  runtime, but `save` serializes the whole ramfs tree to a flat blob and writes it
  to `aeos_fs.img` on the host through ARM semihosting. On the next boot it is read
  back and the tree is reconstructed, so a file written in one session is there in
  the next without any block device or real disk driver.

### A userspace program killed from the shell

This works today. The ELF loader and the EL0 boundary it depends on are both
shipped, and the shell drives them:

- `exec /hello` loads a static ELF64 off the filesystem and runs it at EL0. The
  embedded `/hello` is also run once on boot, so the serial log shows the loader
  mapping it, the binary printing `hello, EL0!` through a `write` syscall, and
  control returning to the kernel.
- `ps` lists the loaded program in the process registry alongside the kernel
  threads, with its pid, state, CPU, ticks, and heap bytes.
- `kill <pid>` reaps a registered process: it sets a flag the syscall handler
  honors at the program's next `svc` boundary.

All three are proven headlessly by the test suite (`test_elf_load_run`,
`test_process_kill_reap`, and the EL0 round-trip scenarios), which `make audit`
gates. What is *not* here is a screenshot or screen recording of the sequence:
capturing one needs a GPU-backed display, and the build and CI environment is
headless. The visual capture is a known display-dependent follow-up, to be done
on a machine with a display. No screenshot or clip has been fabricated to stand
in for it.

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
- **Virtual Memory**: MMU enabled from `vmm.c` with an identity map (Normal-WB RAM, Device-nGnRnE MMIO) plus a TTBR1 high-half alias; the kernel runs identity-mapped
- **Memory Management**: Buddy allocator for physical memory, first-fit heap allocator
- **Process Management**: Round-robin scheduler with context switching (100 Hz timer); a scheduler-independent process registry backs `ps` and `kill`
- **Userspace**: EL0/EL1 boundary; a static ELF64 loaded from a file runs at EL0 with per-segment W^X and reaches the kernel only through trapped `svc #0`
- **SMP**: Secondary cores brought online via PSCI CPU_ON (four cores on the production boot); the scheduler runqueue is spinlock-protected
- **Interrupts**: ARM GICv2 + Generic Timer; the virtual-timer PPI is delivered as an IRQ and serviced through `handle_irq`
- **System Calls**: One dispatcher, two paths (direct C call at EL1, `svc #0` trap from EL0): exit, write, read, getpid, yield
- **Networking**: Minimal in-kernel Ethernet/ARP/IPv4/ICMP stack over a poll-driven virtio-net driver; `ping` gets an ICMP echo reply
- **Real-time Clock**: PL031 RTC drives the taskbar wall clock and the boot log
- **Diagnostics**: Symbol-aware backtrace with DWARF `file:line` resolution; crash-log persistence over semihosting
- **Filesystem**: VFS abstraction layer with ramfs implementation and host persistence
- **Text Editor**: Vim-like modal editor with insert/normal/ex modes
- **Interactive Shell**: 30 built-in commands with colorized output and history that persists across reboots

### Device Drivers
- **VirtIO GPU**: Framebuffer graphics (640x480 @ 32bpp)
- **VirtIO Input**: Mouse and keyboard support via VirtIO MMIO
- **VirtIO Net**: Legacy virtio-net (MAC-only negotiation, poll-driven RX/TX) under the in-kernel IP stack
- **PL011 UART**: Serial console for text mode
- **PL031 RTC**: Wall-clock time at `0x09010000` for the taskbar clock
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

All `run*` targets launch QEMU with `-smp 4` and a virtio-net device
(`-netdev user,id=net0 -device virtio-net-device,netdev=net0`), so the SMP
bringup and the `ping 10.0.2.2` path are live out of the box.

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
- **Alt + F4**: Close the focused window. Same fade-out path as the window's close button; the app's `on_close` runs after the close animation completes.
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
| ps | List processes (pid, CPU, state, ticks, heap bytes, name) |
| exec | Load and run a static ELF64 at EL0 |
| kill | Kill a process by PID |
| meminfo | Memory statistics |
| uptime | System uptime |
| irqinfo | Interrupt statistics |
| ping | Ping an IPv4 host with ICMP echo (defaults to 10.0.2.2) |
| history | Command history |
| time | Time command execution |
| uname | System information |
| startx | Start the graphical desktop environment |
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

`make test` builds the kernel with `TEST=1`, which links a self-contained test runner (`src/kernel/test_runner.c`) as `kernel_main` instead of the normal entry point. The runner brings up memory, the VFS, and the process subsystem, then exercises PMM, heap, spinlock, VFS, process, ps-accounting, shell-parse, history-persist, symbol-lookup, framebuffer, the EL0 round trip and privileged-instruction trap, the ELF loader and kill-reap, SMP runqueue-lock and `last_cpu`, the net ARP/ICMP/RX-bounds path, PL031 time formatting, the DWARF `file:line` backtrace, and security smoke scenarios, 39 in total. The five security scenarios cover the 13.B audit invariants: kcalloc overflow rejection, stack-guard sentinel detection, double-free-after-merge refusal, VFS path-length rejection, and editor growth-overflow refusal. Each scenario logs `PASS: <name>` or `FAIL: <name> (<why>)`. After the suite finishes the runner prints `TEST RESULTS: P PASSED, F FAILED` and exits via semihosting; the `test` target captures stdout to `build/test.log`, parses that line, and exits 0 only when the failure count is zero. Run takes well under a second.

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
0x09010000  RTC (PL031)
0x0a000000  VirtIO devices (GPU, net, keyboard, mouse)
```

All MMIO above is reached through the identity-mapped Device-nGnRnE block; the
MMU is on, so these are virtual addresses equal to their physical ones.

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

- **Kernel W^X**: The kernel image runs from one coarse RWX 1 GB block; per-segment W^X exists only for loaded EL0 code, not the kernel itself
- **Userspace concurrency**: One EL0 program runs at a time, synchronously, in a single 2 MB user window. `kill` reaps a registered process at its next syscall, but interactive kill of a running program from a second prompt needs preemptive scheduling
- **Cross-core scheduling**: Secondary cores come online and park; the round-robin scheduler runs on the primary, so there is no cross-core preemption yet
- **Networking**: ARP and ICMP echo only, slirp-only, no TCP/UDP/DHCP/DNS and no socket layer
- **Persistence integrity**: Filesystem and shell-history persistence trust the host through ARM semihosting
- **Shell Input**: Arrow keys not functional in text mode (escape sequences disabled)
- **GUI Applications**: Some app functionality is basic/placeholder

## Documentation

Detailed implementation documentation is available in `docs/`. Each section covers a specific kernel subsystem with code walkthroughs and API references.

## License

MIT License. See `LICENSE` file for details.
