# Security

## Threat model

AEOS is an educational kernel. **Do not deploy it.** It is built to be read and modified, not to defend against attackers. The threat model below exists so contributors can reason about which classes of bug are real bugs versus expected behaviour.

### What AEOS deliberately does not protect against

- **Privilege boundaries.** All code runs at EL1. There is no userspace, no EL0, no `SVC` trap, no per-process address space. Anything the shell can do, any application can do, including writing arbitrary kernel memory.
- **Memory isolation.** No MMU, no page tables. Any pointer can address any physical RAM in the QEMU virt machine. A bug in one app can corrupt another.
- **Network attacks.** There is no network driver and no IP stack.
- **Multi-tenant isolation.** A single CPU runs the kernel and every "process". Secondary cores are parked at boot.
- **Cryptographic guarantees.** No crypto primitives, no signed binaries, no integrity verification beyond the heap-block magic and the Tetris-save checksum.
- **Persistence integrity.** Filesystem persistence uses ARM semihosting, which trusts the host. A compromised host can write anything to `aeos_fs.img`.

### What we do try to get right

These are the invariants that, if broken, count as bugs worth reporting:

- **The kernel does not crash on shell input.** Even malformed commands, oversized arguments, or corrupted state should produce `[ERROR]` output, not a synchronous exception.
- **`make test` stays green** across host platforms (Linux, WSL, macOS with the documented toolchain).
- **The heap detects double-free** via the per-block magic field (`0xAEDA110C`). A `kfree` on a freed pointer should `klog_error`, not silently corrupt the free list.
- **Frame-pointer-walking backtrace** does not dereference out-of-range addresses. It bounds-checks every saved FP against `[_kernel_start, __stack_top)` before reading.
- **Persistence formats reject corrupted files.** Tetris's `/tetris_high.bin` and the ramfs save format both refuse to load on bad magic, version mismatch, or checksum failure, and fall back to defaults.

The 13.B security audit added three more defended invariants:

- **Kernel stack overflow is detected.** A magic sentinel at the bottom of the boot stack is checked on every exception entry and timer tick; a clobbered sentinel triggers a deterministic `klog_fatal` naming the offending PC instead of silently corrupting BSS.
- **Allocation paths reject pathologically large inputs.** The editor refuses a line-array growth whose byte size would overflow, and the VFS rejects a path over `VFS_PATH_MAX` or a component over `MAX_FILENAME_LEN - 1` before allocating, so attacker-influenced shell input cannot drive an undersized buffer or an unbounded `kmalloc`.
- **Heap-block magic is maintained across every header-touching operation.** `split_block`, `merge_free_blocks`, and the in-place `krealloc` shrink re-stamp the surviving block, and `kfree` refuses a pointer whose header magic is wrong, so the merge-then-free worst case is detected rather than corrupting the free list.

### Invariant sign-off

Each invariant the 13.B audit closed is proven by an automated scenario in the in-kernel test runner (`src/kernel/test_runner.c`) and gated by `make audit`. The verification column names the scenario or the review check.

| ID | Invariant | Verification | Closed |
|----|-----------|--------------|--------|
| SEC-01 | Kernel stack overflow is detected and panics naming the PC | `make audit` / `test_sec_stack_guard` | 13.B |
| SEC-02 | Editor line-array growth overflow is refused, buffer intact | `make audit` / `test_sec_editor_growth_overflow` | 13.B |
| SEC-03 | VFS path over the length bound or with an over-long component is rejected before allocation | `make audit` / `test_sec_vfs_path_too_long` | 13.B |
| SEC-04 | kprintf crash-dump ring interleave invariant documented, write barrier in place | code review of `kprintf.c` (`dmb ish` after the ring store, DAIF-mask invariant documented) | 13.B |
| SEC-05 | inode refcount increment is ordered before the inode is handed out | code review of `vfs.c` (`dmb ish` between `refcount++` and `vfs_fd_alloc`); existing VFS open scenarios | 13.B |
| SEC-06 | Heap magic re-stamped on split/merge/shrink; `kfree` refuses a wrong-magic pointer | `make audit` / `test_sec_double_free_after_merge`, `test_sec_kcalloc_overflow` | 13.B |
| SEC-07 | `process_create` copies the name into a fixed PCB buffer, not the caller pointer | `make audit` / `test_process_create_remove` (mutate-after-create) | 13.B |

## Reporting an issue

If you find a way to make AEOS crash, hang, or misbehave on a path that the threat model says we try to get right, open a GitHub issue with:

1. The exact `make` invocation that produces the failure.
2. The kernel boot log up to and including the failure (and `build/crash.log` if the kernel halted on an exception).
3. Your host OS, QEMU version, and toolchain version.
4. A reproduction recipe, the smallest sequence of shell commands or app interactions that triggers it.

There is no embargo period; this is an educational kernel, not a deployed system. Public issues are fine.

## Reporting a vulnerability in something AEOS depends on

AEOS depends on QEMU, GCC's AArch64 backend, GNU `binutils`, and `m4`. If you suspect a vulnerability in one of those, report it upstream. That is not an AEOS issue.
