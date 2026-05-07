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

## Reporting an issue

If you find a way to make AEOS crash, hang, or misbehave on a path that the threat model says we try to get right, open a GitHub issue with:

1. The exact `make` invocation that produces the failure.
2. The kernel boot log up to and including the failure (and `build/crash.log` if the kernel halted on an exception).
3. Your host OS, QEMU version, and toolchain version.
4. A reproduction recipe — the smallest sequence of shell commands or app interactions that triggers it.

There is no embargo period; this is an educational kernel, not a deployed system. Public issues are fine.

## Reporting a vulnerability in something AEOS depends on

AEOS depends on QEMU, GCC's AArch64 backend, GNU `binutils`, and `m4`. If you suspect a vulnerability in one of those, report it upstream — that is not an AEOS issue.
