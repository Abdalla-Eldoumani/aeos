# Contributing

AEOS is an educational kernel. Patches that improve clarity, correctness, or coverage are welcome; patches that add scope without raising the floor are not.

## Before you start

- Read `README.md` for what AEOS is and the bounded form each subsystem takes (MMU on, an EL0/EL1 boundary running a loaded ELF, SMP cores online, a minimal ARP/ICMP net stack, a PL031 wall clock) plus the honest remaining gaps (no kernel W^X, no cross-core preemptive scheduling, no TCP/UDP/DHCP/DNS).
- Read the `CLAUDE.md` at the repo root and the one in the subdirectory you plan to touch. They describe the current architecture as it actually is.
- Read the matching section under `docs/` for a code walkthrough.

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
- [ ] Manual run-through in QEMU for any UI change, with a screenshot in the PR description.

## Reporting bugs

Open an issue with: the exact `make` invocation, the kernel boot log up to the failure, and the host OS / QEMU version. If you have a backtrace from `build/crash.log`, paste that too.

## License

By contributing you agree your changes are released under the same MIT license as the rest of the project (see `LICENSE`).
