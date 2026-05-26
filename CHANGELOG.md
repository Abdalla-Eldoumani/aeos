# Changelog

All notable changes to AEOS, in reverse chronological order. The project predates this changelog; the entries below cover work tracked under the `.agent/TASKS.md` phase plan.

## [Phase 13] - 2026-05

The Phase 13 milestone added the kernel foundations AEOS previously did without: an MMU, an EL0/EL1 boundary running a loaded ELF, an SMP bringup, a network stack, and a wall clock. Each shipped in a bounded form documented in `ARCHITECTURE.md` and the subsystem `CLAUDE.md` files, and each is proven headlessly by the in-kernel test suite (now 39 scenarios, gated by `make audit`).

### Added
- **FEAT-01 MMU.** `src/mm/vmm.c` builds an identity map (RAM as a Normal-WB cacheable 1 GB block, the low MMIO window as a Device-nGnRnE block) plus a TTBR1 high-half alias of RAM, and enables the MMU and caches from `kernel_main`. The kernel runs identity-mapped; writing the framebuffer still lights pixels. There is no kernel-wide W^X (one RWX block); per-segment W^X exists only for loaded EL0 code.
- **FEAT-02 EL0/EL1 boundary.** A process runs at EL0 and makes syscalls via `svc #0` trapped into the EL1 vector table (`el0_aarch64_sync`) and routed through the same `syscall_handler` as the EL1 direct-call path. A privileged instruction from EL0 traps to EL1 (EC=0x18). Proven by `test_el0_roundtrip` and `test_el0_priv_trap`; the production boot shows the round-trip markers.
- **FEAT-03 ELF loader + `exec`/`kill`.** `elf_exec_file` parses a static ELF64 off the VFS (`elf_validate`, reject-never-fault), maps each `PT_LOAD` into the 2 MB EL0 window with per-segment W^X (executable read-only at EL0), zero-fills BSS, and runs it at EL0. The loaded program is registered in a scheduler-independent process registry, so `ps` lists it and `kill <pid>` reaps it at its next `svc` boundary. The `exec` and `kill` shell commands are wired. The embedded `/hello` is written to ramfs and run once on boot.
- **FEAT-04 SMP.** `smp_init` brings up secondary cores 1 through 3 via PSCI CPU_ON with a bounded handshake (a stuck or failed secondary is logged and skipped; the primary always reaches the WM loop). The scheduler runqueue is spinlock-protected, the kprintf crash-dump ring lock discharges the deferred SEC-04 invariant, and `ps` gains a CPU column. Four cores online on the production boot. Cross-core preemptive scheduling is out of scope.
- **FEAT-05 virtio-net + `ping`.** A legacy virtio-net driver (MAC-only negotiation, poll-driven RX/TX) under a minimal in-kernel Ethernet/ARP/IPv4/ICMP stack answers ARP for its own address and gets an ICMP echo reply. The `ping` command parses a dotted-quad (defaulting to the slirp gateway 10.0.2.2), is bounded so it never hangs the prompt, and the boot path runs a one-shot `ping 10.0.2.2` demo. No TCP/UDP/DHCP/DNS/sockets. The RX parser bounds-checks every attacker-influenced field.
- **FEAT-06 PL031 clock, DWARF backtrace, persistent history, `ps` accounting.** A PL031 RTC driver reads the host wall-clock seconds and drives the taskbar clock (replacing the uptime counter) and the boot log. The backtrace resolves `file:line` from a build-time DWARF line table. Shell history persists across reboots through semihosting (`aeos_hist.img`, validated on load). `ps` shows real per-tick `TICKS` and per-PCB `HEAP_B` columns.

### Changed
- `README.md`, `SECURITY.md`, and the `docs/` walkthroughs updated to describe the shipped kernel: the Scope, Known Limitations, Core OS Features, and Shell Commands (now 30) blocks in `README.md`; the threat posture and the invariant sign-off table (extended with the Phase 6-9 invariants) in `SECURITY.md`.
- `ARCHITECTURE.md` added during the milestone as the one-page boot-order and data-flow model.

### Fixed (post-milestone GUI polish, 2026-05-26)
- **30 FPS compositor (BUG-20).** `wm_run` now re-renders the desktop on every 33 ms frame tick, not only on input events. Previously content re-rendered only when an event set `needs_redraw`, so between events the loop re-pushed a stale framebuffer: the taskbar clock froze and a freshly opened window stayed on the first frame of its slide-in (a blank rectangle) until a drag. The clock, animations, and live widgets now advance without input.
- **Absolute pointer.** `run-ramfb` attaches `virtio-tablet-device` instead of the relative `virtio-mouse-device`, so the guest cursor tracks the host pointer and a click lands on the row aimed at (the driver already had the scaled `EV_ABS` path).
- **Local-time clock.** `run-ramfb` runs QEMU with `-rtc base=localtime`, so the taskbar shows the host's local time (with DST) rather than UTC; the kernel labels read "local". A bare-metal kernel has no IP/GPS geolocation, so the host timezone is the automatic source. `pl031_format_hms` stays timezone-agnostic.
- **Clock layout.** The taskbar clock moved to `FB_WIDTH - 72` so the 8-character `HH:MM:SS` no longer clips at the right edge.

### Added (tooling)
- `scripts/screenshot.py`: headless GUI verification via QEMU QMP `screendump` (captures the virtio-gpu scanout under `-display none`) plus `input-send-event` (tablet clicks). This is how the redraw, clock, and click-highlight fixes were verified without a display; documented in `CONTRIBUTING.md`.

## [Unreleased]

### Added
- GitHub Actions CI: `make`, `make TEST=1`, `make test`, and a TODO/FIXME guard on every push and pull request.
- `CONTRIBUTING.md` covering build, style, commit discipline, docs requirements, and a PR checklist.
- `SECURITY.md` describing the threat model (kernel-mode-only, single CPU, no userspace) and how to report issues.
- Issue and pull-request templates under `.github/`.
- README "Scope" section that states what AEOS deliberately is not (no MMU, no userspace, no SMP, no networking, no RTC).
- Tetris GUI app: 10x20 board, seven tetrominoes with proper colors, gravity that scales with level, soft/hard drop, pause, R-to-restart, high score persisted to `/tetris_high.bin` (validated by magic, version, and checksum).
- Shell pipes: `ls | grep foo` and friends, with a 256-byte ring buffer per stage and a `kprintf_output_hook` swap that keeps existing built-ins working unchanged.
- In-kernel test runner under `make TEST=1`: 13 scenarios across PMM, heap, VFS, processes, shell parser, symbol lookup, and framebuffer (since grown to 39 across the Phase 13 subsystems; see the Phase 13 section above).
- Symbol-aware kernel backtrace: two-pass build embeds a sorted address-to-name table; `handle_exception` resolves PC and walks the FP chain to print named frames.
- Crash-log persistence: 4 KB ring buffer of recent kprintf output, dumped to `build/crash.log` via semihosting on panic.

### Changed
- README features list, app icon list, and source tree now include Tetris.
- Top-level `docs/README.md` and `docs/10-gui-applications/README.md` document the Tetris app.
- `docs/02-memory-management/README.md` memory layout diagram corrected to the current heap-then-stack linker order.
- `docs/03-interrupts-exceptions/implementation.md` rewritten to use the virtual timer (CNTV) and IRQ 27 the kernel actually uses, plus the GICv2 spurious-IRQ filter at threshold 1020.
- `docs/08-graphics-gui/implementation.md` snippets reflect direct compositing (no per-window backbuffer), the close-fade reaping path, and the notify render hook.
- `docs/10-gui-applications/implementation.md` shows the callback-null-before-unregister pattern in `tetris_close` etc., not the unsafe pattern.

### Removed
- `process_get_by_pid` stub: deleted from the public header and the implementation. The API was never wired up; if a future shell command needs PID lookup, add a real index.
- `#if 0` "advanced shell features" block in `src/kernel/shell.c` (~250 lines of dead code that the banner used to advertise as "tab completion").
- Two commented-out helper-function blocks (`print_exception_counters`, `puts_syscall`) in `src/kernel/main.c`.
- Debug `kprintf` and three `[ASM DEBUG]` strings in `src/interrupts/vectors.asm`.
- All seven `TODO`/`FIXME` markers from tracked source, replaced with real fixes (`cd` type check, Tetris save format) or honest in-place comments explaining why the limitation exists.

### Fixed
- `cd <path>` to a non-directory file now prints `cd: <path>: Not a directory` instead of silently changing the working directory.
- Shell banner and `klog_info` no longer claim "tab completion"; the input loop never wired Tab in.
- Tetris high score file rejects truncated, mismatched, or corrupted contents instead of returning garbage.
- Top-level `README.md` memory map matches current `__heap_start` / `__heap_end` / `__stack_top` (was off by ~16 KB after a kernel-size change).
