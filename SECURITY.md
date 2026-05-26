# Security

## Threat model

AEOS is an educational kernel. **Do not deploy it.** It is built to be read and modified, not to defend against attackers. The threat model below exists so contributors can reason about which classes of bug are real bugs versus expected behaviour.

### What AEOS deliberately does not protect against

The kernel now has an MMU, an EL0/EL1 boundary, an SMP bringup, and a small network
stack, so the posture below is the *current* one, not a list of absent subsystems.

- **Kernel W^X.** The MMU is on, but the kernel's code, rodata, data, heap, and stack share one RWX 1 GB block. The kernel can write and then execute its own image. Per-segment W^X applies only to loaded EL0 code (executable segments are mapped read-only at EL0), not the kernel itself.
- **The kernel's own memory from the EL0 program.** The EL0/EL1 boundary is real: a loaded ELF runs at EL0, faults on every kernel address, and reaches the kernel only through trapped `svc`. But there is no per-process isolation between concurrent userspace programs, because only one EL0 program runs at a time in a single static 2 MB window. `copy_from_user`-style validation covers `sys_write` only; other syscalls do not yet validate user pointers.
- **Network attacks.** The stack is Ethernet, ARP, IPv4, and ICMP echo only, over a poll-driven virtio-net device on QEMU slirp. There is no TCP, UDP, DHCP, DNS, or socket layer. The RX path bounds-checks every attacker-influenced field before indexing and drops on any inconsistency, but the stack is not hardened beyond not faulting on a crafted frame.
- **Multi-tenant isolation.** Secondary cores come online via PSCI and park in `wfe`; the round-robin scheduler runs on the primary. There is no cross-core preemptive scheduling and no isolation between "processes" sharing the kernel address space.
- **Cryptographic guarantees.** No crypto primitives, no signed binaries, no integrity verification beyond the heap-block magic, the persistence-format magic/version/checksum, and the Tetris-save checksum.
- **Persistence integrity.** Filesystem and shell-history persistence use ARM semihosting, which trusts the host. A compromised host can write anything to `aeos_fs.img` or `aeos_hist.img`.

### What we do try to get right

These are the invariants that, if broken, count as bugs worth reporting:

- **The kernel does not crash on shell input.** Even malformed commands, oversized arguments, or corrupted state should produce `[ERROR]` output, not a synchronous exception.
- **`make test` stays green** on the verified host platforms (Linux and WSL). The macOS toolchain is documented in `README.md` but is not part of the verified set; the macOS path has not been run for this audit.
- **The heap detects double-free** via the per-block magic field (`0xAEDA110C`). A `kfree` on a freed pointer should `klog_error`, not silently corrupt the free list.
- **Frame-pointer-walking backtrace** does not dereference out-of-range addresses. It bounds-checks every saved FP against `[_kernel_start, __stack_top)` before reading.
- **Persistence formats reject corrupted files.** Tetris's `/tetris_high.bin` and the ramfs save format both refuse to load on bad magic, version mismatch, or checksum failure, and fall back to defaults.

The 13.B security audit added three more defended invariants:

- **Kernel stack overflow is detected.** A magic sentinel at the bottom of the boot stack is checked on every exception entry and timer tick; a clobbered sentinel triggers a deterministic `klog_fatal` naming the offending PC instead of silently corrupting BSS.
- **Allocation paths reject pathologically large inputs.** The editor refuses a line-array growth whose byte size would overflow, and the VFS rejects a path over `VFS_PATH_MAX` or a component over `MAX_FILENAME_LEN - 1` before allocating, so attacker-influenced shell input cannot drive an undersized buffer or an unbounded `kmalloc`.
- **Heap-block magic is maintained across every header-touching operation.** `split_block`, `merge_free_blocks`, and the in-place `krealloc` shrink re-stamp the surviving block, and `kfree` refuses a pointer whose header magic is wrong, so the merge-then-free worst case is detected rather than corrupting the free list.

### Invariant sign-off

Each invariant below is proven by an automated scenario in the in-kernel test runner (`src/kernel/test_runner.c`) and gated by `make audit`, or by a named code review. SEC-01 through SEC-07 are the invariants the 13.B hardening pass closed; the rows below SEC-07 are the security-relevant invariants the Phase 6-9 feature work (the ELF loader, SMP, the net stack, and the Phase 9 polish) added. The verification column names the scenario or the review check. The current suite is 39 scenarios, `39 PASSED, 0 FAILED` under `-smp 4` with the net device.

| ID | Invariant | Verification | Closed |
|----|-----------|--------------|--------|
| SEC-01 | Kernel stack overflow is detected and panics naming the PC | `make audit` / `test_sec_stack_guard` | 13.B |
| SEC-02 | Editor line-array growth overflow is refused, buffer intact | `make audit` / `test_sec_editor_growth_overflow` | 13.B |
| SEC-03 | VFS path over the length bound or with an over-long component is rejected before allocation | `make audit` / `test_sec_vfs_path_too_long` | 13.B |
| SEC-04 | kprintf crash-dump ring interleave invariant documented, write barrier in place | code review of `kprintf.c` (`dmb ish` after the ring store, DAIF-mask invariant documented) | 13.B |
| SEC-05 | inode refcount increment is ordered before the inode is handed out | code review of `vfs.c` (`dmb ish` between `refcount++` and `vfs_fd_alloc`); existing VFS open scenarios | 13.B |
| SEC-06 | Heap magic re-stamped on split/merge/shrink; `kfree` refuses a wrong-magic pointer | `make audit` / `test_sec_double_free_after_merge`, `test_sec_kcalloc_overflow` | 13.B |
| SEC-07 | `process_create` copies the name into a fixed PCB buffer, not the caller pointer | `make audit` / `test_process_create_remove` (mutate-after-create) | 13.B |
| SEC-09 | The ELF loader rejects a malformed or non-ELF file rather than faulting (`elf_validate` reads every field behind the size gate; the phdr-table extent is computed overflow-safe in `uint64_t`) | `make audit` / `test_elf_reject_malformed` | Phase 6 (FEAT-03) |
| SEC-10 | Loaded segments and the EL0 stack are bounds-checked against `USER_L3_TOP` (the real 2 MB mapping ceiling), required strictly ascending and non-overlapping; a segment past the window is rejected before any page is mapped | `make audit` / `test_elf_reject_oversized_segment` (a 3 MB-spanning PT_LOAD is refused with no leaked pmm pages) | Phase 6 (FEAT-03, review CR-01/CR-03) |
| SEC-11 | The loader's page tracker fails closed: reaching the page-count ceiling rejects the run and frees every mapped page rather than leaking past the cap | code review of `exec.c` (`free_mapped_pages` on every error path; `mapped_pas[]` sized to the real ceiling) | Phase 6 (FEAT-03, review CR-02) |
| SEC-12 | `sys_write` from EL0 bound-checks the user buffer against the mapped window (range plus `ptr+len` overflow) before any dereference; the check is gated on `el0_oneshot_active()` so the EL1 direct-call path is unaffected | code review of `syscall.c` (`is_user_range` gate); the EL0 round-trip scenarios | Phase 6 (FEAT-03) |
| SEC-13 | The scheduler runqueue, the kprintf crash-dump ring, and the net RX/TX queues are spinlock-protected for cross-core safety (SEC-04's deferred "real lock on the ring" is now discharged under SMP) | `make audit` / `test_spinlock_uncontended`, `test_smp_runqueue_lock`, `test_kprintf_ring_panic_bypass` (the panic-path trylock-or-bypass does not deadlock) | Phase 7 (FEAT-04) |
| SEC-14 | `net_rx_dispatch` bounds-checks every attacker-influenced field before indexing and drops on any inconsistency, including the lower bound on `total_length` that prevents the `icmp_len` unsigned underflow | `make audit` / `test_net_rx_bounds` (sub-Ethernet, sub-ARP, oversized-IHL, and underflow frames all return without faulting) | Phase 8 (FEAT-05, review CR-01) |
| SEC-15 | `history_load` validates magic, version, count, and record length before indexing any record, falling back to an empty ring on a missing, foreign, or malformed image so the boot path never hangs or faults | `make audit` / `test_history_persist_roundtrip` | Phase 9 (FEAT-06) |
| SEC-16 | `line_lookup` writes an empty string on an empty table, a below-base address, a below-first key, or a miss; `snprintf`'s buffer size caps the file:line copy, so the panic-path backtrace never reads out of range | `make audit` / `test_backtrace_fileline`; code review of `backtrace.c` | Phase 9 (FEAT-06) |

## Reporting an issue

If you find a way to make AEOS crash, hang, or misbehave on a path that the threat model says we try to get right, open a GitHub issue with:

1. The exact `make` invocation that produces the failure.
2. The kernel boot log up to and including the failure (and `build/crash.log` if the kernel halted on an exception).
3. Your host OS, QEMU version, and toolchain version.
4. A reproduction recipe, the smallest sequence of shell commands or app interactions that triggers it.

There is no embargo period; this is an educational kernel, not a deployed system. Public issues are fine.

## Reporting a vulnerability in something AEOS depends on

AEOS depends on QEMU, GCC's AArch64 backend, GNU `binutils`, and `m4`. If you suspect a vulnerability in one of those, report it upstream. That is not an AEOS issue.
