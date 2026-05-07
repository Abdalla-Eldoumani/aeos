# Changelog

All notable changes to AEOS, in reverse chronological order. The project predates this changelog; the entries below cover work tracked under the `.agent/TASKS.md` phase plan.

## [Unreleased]

### Added
- GitHub Actions CI: `make`, `make TEST=1`, `make test`, and a TODO/FIXME guard on every push and pull request.
- `CONTRIBUTING.md` covering build, style, commit discipline, docs requirements, and a PR checklist.
- `SECURITY.md` describing the threat model (kernel-mode-only, single CPU, no userspace) and how to report issues.
- Issue and pull-request templates under `.github/`.
- README "Scope" section that states what AEOS deliberately is not (no MMU, no userspace, no SMP, no networking, no RTC).
- Tetris GUI app: 10x20 board, seven tetrominoes with proper colors, gravity that scales with level, soft/hard drop, pause, R-to-restart, high score persisted to `/tetris_high.bin` (validated by magic, version, and checksum).
- Shell pipes: `ls | grep foo` and friends, with a 256-byte ring buffer per stage and a `kprintf_output_hook` swap that keeps existing built-ins working unchanged.
- In-kernel test runner under `make TEST=1`: 13 scenarios across PMM, heap, VFS, processes, shell parser, symbol lookup, and framebuffer.
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
- All seven `TODO`/`FIXME` markers from tracked source — replaced with real fixes (`cd` type check, Tetris save format) or honest in-place comments explaining why the limitation exists.

### Fixed
- `cd <path>` to a non-directory file now prints `cd: <path>: Not a directory` instead of silently changing the working directory.
- Shell banner and `klog_info` no longer claim "tab completion" — the input loop never wired Tab in.
- Tetris high score file rejects truncated, mismatched, or corrupted contents instead of returning garbage.
- Top-level `README.md` memory map matches current `__heap_start` / `__heap_end` / `__stack_top` (was off by ~16 KB after a kernel-size change).
