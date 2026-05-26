# Contributing

AEOS is an educational kernel. Patches that improve clarity, correctness, or coverage are welcome; patches that add scope without raising the floor are not.

## Before you start

- Read `README.md` for what AEOS is and the bounded form each subsystem takes (MMU on, an EL0/EL1 boundary running a loaded ELF, SMP cores online, a minimal ARP/ICMP net stack, a PL031 wall clock) plus the honest remaining gaps (no kernel W^X, no cross-core preemptive scheduling, no TCP/UDP/DHCP/DNS).
- Read `ARCHITECTURE.md` for boot order, subsystem dependencies, and the runtime data flows, then the matching section under `docs/` for a code walkthrough of the area you plan to touch. These are the authoritative in-repo references.
- If your checkout has the maintainer's local `CLAUDE.md` aids (repo root and per-subdirectory; these are kept out of git by design), read the relevant ones too - they restate the same architecture with extra implementation notes.

## Build and test

```bash
make            # build kernel.elf
make run-ramfb  # graphical desktop (recommended)
make run        # text mode over UART
make test       # build TEST=1, run the in-kernel test runner, exit 0 only on all-pass
make clean
```

The `run*` targets launch QEMU with `-smp 4` and a virtio-net device (`-netdev user,id=net0 -device virtio-net-device,netdev=net0`), so the SMP bringup and the `ping 10.0.2.2` path are live; the kernel also boots without the net device (the probe no-ops).

`make test` is the gate. CI runs `make`, `make TEST=1`, `make test`, and a `grep` that fails the build on any new `TODO`/`FIXME`/`XXX`/`HACK` marker in tracked source.

A clean headless boot is the second check (no display needed): boot the production kernel and confirm it reaches the WM loop without faulting.

```bash
timeout 25 qemu-system-aarch64 -M virt -cpu cortex-a57 -m 256M -smp 4 \
  -device virtio-gpu-device -device virtio-keyboard-device -device virtio-tablet-device \
  -netdev user,id=net0 -device virtio-net-device,netdev=net0 \
  -display none -serial stdio -semihosting-config enable=on,target=native \
  -rtc base=localtime -kernel kernel.elf
# healthy: timeout exit 124 + "Starting window manager main loop" + no panic/abort
```

## Verifying the GUI without a display

`make test` and the boot check confirm the code compiles, links, and reaches the WM loop. They do NOT confirm the rendered pixels are right (a redraw that never fires, a window that paints blank, a click that lands on the wrong row). Verify those headlessly with `scripts/screenshot.py`, which drives QEMU over QMP: it captures the virtio-gpu scanout with `screendump` (works under `-display none`) and injects tablet clicks with `input-send-event`.

```bash
make
# Two captures 2.6 s apart with no input: the taskbar clock must differ between
# them (proves the 30 FPS redraw advances live content without an event).
python3 scripts/screenshot.py --name clock --shots 2 --interval 2.6 --out /tmp

# Open an app (double-click its icon centre) and confirm the window shows content.
python3 scripts/screenshot.py --name term --double 44,44 --out /tmp   # Terminal

# Open Files (icon 1) and confirm a clicked entry highlights with no drag.
python3 scripts/screenshot.py --name files --double 124,44 --click 250,142 --before --out /tmp
```

Open the PNGs and check the rendered result. This is how the redraw, clock, and click-highlight fixes were verified; do the same for any rendering, input, compositing, or window-behaviour change instead of asserting it from a green build.

## Coding style

- C, freestanding, no libc, no FP/SIMD (`-mgeneral-regs-only`).
- File header banner on every `.c`: project name, file path, one-line description.
- Function docstrings explain *why*, not *what*. Skip docstrings on obvious functions.
- No globals outside file scope. Use `static` for file-private state.
- Allocate via `kmalloc`/`kfree` only. Raw page calls live inside `mm/`.
- `klog_info`/`klog_warn`/`klog_error`/`klog_fatal` for diagnostics. Plain `kprintf` only when you specifically don't want a `[INFO]` prefix.
- Don't write marketing language: no "robust", "seamless", "leverage", "comprehensive", "powerful". Describe what the code does.
- Don't write `// TODO:` markers; fix the issue or write a real comment that explains why the limitation exists.

## Commit discipline

- One file per commit. One logically inseparable change per commit.
- Lowercase commit messages, imperative mood, brief.
- No batched diffs. CI will merge a one-file commit; review will not merge a 10-file refactor.
- Never `--amend` after a hook fails; make a new commit.
- Never push `--force` to `master`/`main`.

## Documentation

- If you change behaviour described in `docs/<section>/README.md` or `implementation.md`, update both in the same change.
- If you add a `.c` file to a subsystem, add it to the table in that subsystem's `CLAUDE.md`.
- If you change the boot order in `kernel_main`, mirror that in `src/kernel/CLAUDE.md`.

## Pull request checklist

- [ ] `make` succeeds clean.
- [ ] `make test` reports `39 PASSED, 0 FAILED` (or however many scenarios exist when you read this).
- [ ] No new `TODO`/`FIXME`/`XXX`/`HACK` markers.
- [ ] Subsystem `CLAUDE.md` and matching `docs/` section reflect the change.
- [ ] Commit history is one-file-per-commit, lowercase imperative messages.
- [ ] Any UI change verified visually: `scripts/screenshot.py` capture (or a manual `make run-ramfb` run) with the screenshot in the PR description.

## Reporting bugs

Open an issue with: the exact `make` invocation, the kernel boot log up to the failure, and the host OS / QEMU version. If you have a backtrace from `build/crash.log`, paste that too.

## License

By contributing you agree your changes are released under the same MIT license as the rest of the project (see `LICENSE`).
