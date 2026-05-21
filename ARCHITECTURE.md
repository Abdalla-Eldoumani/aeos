# AEOS Architecture

This document is the one-page mental model of how AEOS boots and how data moves
through it. Read it before diving into a subsystem; the per-section walkthroughs
in `docs/` go deeper.

It describes the kernel as it stands today: a single-address-space AArch64 kernel.
The MMU is enabled with an identity map plus a high-half alias (described below).
There is no general userspace and no SMP or networking, and the kernel does not
yet enforce per-section W^X. The EL0/EL1 privilege boundary itself, however, is
now established and tested: a minimal in-kernel payload runs at EL0, reaches the
kernel only through trapped `svc` syscalls, and a privileged instruction from EL0
faults to EL1 (see the EL0/EL1 boundary section). A scheduled EL0 process loaded
from a file, W^X, and user-pointer validation are the next milestones.

## Overview

AEOS is a monolithic kernel that runs entirely at EL1 inside QEMU's `virt`
machine. A few properties shape everything else:

- **Identity-mapped single address space.** The MMU is on, but the running
  kernel is identity-mapped: one L1 page table maps RAM as a Normal-WB cacheable
  1GB block and the low MMIO window as a Device-nGnRnE block, so virtual equals
  physical for the kernel. A TTBR1 high-half alias of RAM also exists at
  `0xFFFFFF8000000000` and is demonstrated by reading the kernel image back
  through it, but the kernel does not yet execute from the high half. This is an
  identity map, not a relinked high-half kernel; TTBR0 is reserved for the future
  per-process user mapping. The kernel loads at `0x40000000` and RAM ends at
  `0x50000000` (256 MB).
- **Two ways to make a syscall, one dispatcher.** The kernel itself runs at EL1,
  where a "system call" is a direct C call: `syscall(num, ...)` in
  `src/syscall/syscall.c` is a table lookup, not an `SVC` trap. An EL0 payload,
  by contrast, issues `svc #0`, which traps into the EL1 vector table and is
  routed through the SAME `syscall_handler`. The EL0 path is additive; the GUI
  and shell keep using the direct-call path. The EL0/EL1 boundary is real and
  tested but currently exercised by a one-shot in-kernel payload, not a scheduled
  process (see the EL0/EL1 boundary section).
- **Direct calls across layers.** Apps call window and VFS functions directly.
  There is no message bus and no IPC. The one strict rule on call direction is
  that drivers push events up and never call down into the GUI (see below).
- **Hybrid scheduling.** A 100 Hz ARM Generic Timer FIQ drives the round-robin
  scheduler, but the desktop is one hand-rolled main loop, not a preempted
  process.

## Layers

From the hardware up:

```text
Apps (terminal, files, settings, about, calculator, sysmon, notes, tetris)
  | on_paint / on_key / on_mouse / on_close callbacks
GUI stack: event.c  window.c  wm.c  desktop.c  gui.c  notify.c
  | fb_* primitives          ^ event queue (events flow UP only)
Drivers: framebuffer  virtio_gpu  virtio_input  uart  semihosting
  | QEMU virt MMIO
Hardware @ EL1: GICv2, ARM Generic Timer (FIQ), PL011 UART, VirtIO
```

Alongside the GUI stack sit the core kernel services that everything relies on:
memory management (`src/mm/`, the buddy PMM and the first-fit heap), interrupts
(`src/interrupts/`, the EL1 vector table, GICv2, and the timer), processes
(`src/proc/`, the PCB and round-robin scheduler), the syscall table
(`src/syscall/`), the filesystem (`src/fs/`, the VFS over ramfs plus host
persistence), and the freestanding library stand-ins (`src/lib/`).

**Dependency direction is one-way.** Drivers push input up through `event_push`
(`src/kernel/event.c`) and never call into `wm` or `desktop`. The window manager
pulls events with `event_pop`; it does not reach back into a driver to ask for
input. Apps depend on `window_*`, `wm_*`, and kernel services; nothing in the
lower layers depends on a specific app. Keeping this acyclic is what lets you read
one subsystem without unwinding the whole tree.

## Boot order

`kernel_main` in `src/kernel/main.c` brings up every subsystem in a fixed order.
Do not reorder these without reading what each step depends on; several positions
are load-bearing.

1. `uart_init` so there is a console for early diagnostics.
2. `mm_init`, which runs `pmm_init` then `heap_init`. The heap must exist before
   anything calls `kmalloc`, so this comes early. `mm_init` starts the PMM at
   `__stack_top`, not at the heap end, so the buddy allocator never hands out the
   boot stack's pages and the stack-guard sentinel survives.
3. `vmm_init`, then `vmm_report`. This builds the identity page table plus the
   TTBR1 high-half alias and turns on the MMU and caches. It runs after the heap
   is up and before any driver, the GIC, or the first timer FIQ, so every later
   MMIO and DMA access uses the final mapping. The kernel is identity-mapped, so
   the PC is unchanged across the enable; `vmm_report` prints the SCTLR bits and
   confirms the high-half alias.
4. `interrupts_init` installs the EL1 vector table. This happens before any IRQ
   source is enabled so a stray interrupt cannot land on an empty vector.
5. `gic_init`, then `timer_init`, then `interrupts_enable`, then `timer_start`.
   The controller and timer are configured before interrupts are unmasked, and
   the timer is started last.
6. `vfs_init`, then ramfs creation, then mounting `/`.
7. `init_graphics`, which runs `fb_init` then `virtio_gpu_init`. The framebuffer
   and GPU must be up before anything draws.
8. `process_init` then `scheduler_init`.
9. `syscall_init` to populate the syscall table, then a one-shot EL0 round trip
   (`usermode_run_payload`) that drops to EL0, issues `svc #0` for getpid and
   exit, and returns to the kernel - the serial shows "entered EL0", "svc N from
   EL0", "returned to kernel". It runs once and boot continues; SPSR=0x3C0 masks
   IRQ/FIQ so the running timer cannot preempt the brief EL0 lifetime.
10. `shell_init`.
11. `bootscreen_init`, stage updates, `gui_init`, then `gui_run` when graphical
    mode is available.

If `gui_run` returns, or the user selects text mode at the boot screen, control
falls through to `shell_run`, which reads UART and never returns.

On boot the filesystem is also rehydrated: `fs_load_from_disk` (called from
`kernel_main`) tries to read a saved image back into ramfs. A missing image just
yields a fresh empty filesystem.

## Data flow one: a mouse click to a repaint

This is the single-tick path through the window manager main loop, `wm_run` in
`src/kernel/wm.c`.

1. Each iteration, `wm_run` drains the input devices. `virtio_input_poll`
   (`src/drivers/virtio_input.c`) pulls events off the device queue and the driver
   calls `event_push` (`src/kernel/event.c`) to enqueue each one. The driver does
   not touch the window manager directly.
2. `wm_run` then drains the queue with `event_pop`, handing every event to
   `wm_handle_event`.
3. `wm_handle_event` routes by event type. A mouse button-down goes to
   `handle_mouse_button`.
4. `handle_mouse_button` finds the window under the cursor with `wm_window_at`,
   focuses it with `wm_focus_window`, and calls that window's `on_mouse` callback
   with window-relative coordinates.
5. The app updates its own state and calls `window_invalidate`
   (`src/kernel/window.c`), which sets `WINDOW_FLAG_DIRTY` on the window and calls
   `wm_request_redraw`, marking the WM's redraw flag.
6. Back in the loop, when the redraw flag is set (or on the normal frame cadence),
   `wm_update_display` repaints through `wm_redraw`. `wm_redraw` runs every
   visible window's `on_paint`, composites any active toast notifications above
   the windows, draws the software mouse cursor, and flushes the framebuffer to
   the GPU.

Live-content apps such as the system monitor graph and the notes caret call
`wm_request_redraw` each tick so they get a frame even when no input arrived.

## Data flow two: a shell save to host persistence

This is what happens when a user runs `save` in the shell.

1. The shell dispatches `save` to `cmd_save` (`src/kernel/shell.c`), which gets the
   root filesystem with `vfs_get_root_fs` and calls `fs_save_to_disk`.
2. `fs_save_to_disk` (`src/fs/fs_persist.c`) serializes the in-memory tree into a
   flat buffer. `fs_save` writes a header, then `serialize_inode` walks the ramfs
   inode tree recursively through the VFS, copying each inode's metadata, name,
   and file data into the blob.
3. With the blob built, `fs_save_to_disk` writes it to the host. It opens
   `aeos_fs.img` with `semihost_open`, writes the bytes with `semihost_write`, and
   closes with `semihost_close` (all in `src/drivers/semihosting.c`). Semihosting
   is the only path off the emulated machine to the host filesystem.
4. On the next boot, `fs_load_from_disk` reverses this: it reads `aeos_fs.img`
   back with `semihost_read`, validates the header magic, and `fs_load`
   reconstructs the inode tree via `deserialize_inodes`.

Semihosting blocks the kernel for the duration of the host I/O, which is why a
save is run from the shell or a deliberate call site and not from inside an
interrupt or the per-tick WM loop.

## Constraints

These follow from the properties above and are easy to violate by accident:

- **Single CPU.** Only CPU 0 runs; secondary cores are parked at reset. The
  single-CPU assumption is what makes several lock-free patterns correct today
  (the kprintf crash-dump ring relies on the DAIF mask in the exception handler,
  not a lock). That assumption is revisited when SMP lands.
- **No floating point or SIMD at EL1.** The kernel is built with
  `-mgeneral-regs-only` because CPACR_EL1 does not enable Q-register access, so
  any float or vectorized integer code traps. Animations use Q0.8 fixed-point in
  `src/lib/anim.c`, and the calculator uses int64 fixed-point.
- **Allocators are not interrupt-safe.** `kmalloc`, `kfree`, and the PMM page ops
  must never run from an interrupt handler; an FIQ landing mid-allocation corrupts
  the free lists. Heap block headers carry a magic value so corruption fires a
  fatal log at the site, and `kfree` refuses a caller pointer whose magic is
  wrong, but the rule is to never allocate in interrupt context in the first
  place. The scheduler tick guards against this with an early return before it is
  initialized.
- **Identity mapping, no W^X, no demand paging.** The MMU is enabled but the
  kernel runs through an identity map, so virtual equals physical and there is no
  copy-on-write and no `mmap`. The whole kernel (code, rodata, data, heap, stack)
  lives in one RWX 1GB block, so there is no per-section W^X yet - a later
  milestone once finer-grained kernel tables land. The EL0/EL1 boundary itself IS
  established and tested (kernel pages are EL0-no-access, `svc` is the only EL0
  gateway), but it is not yet full userspace memory safety: there is no
  user-pointer validation in syscalls (the one-shot payload passes no user
  pointers) and no W^X on the user page. The heap is 4 MB and its base moves with
  the kernel image size.
- **File-scope state, no loose globals.** The window manager, event queue,
  scheduler, and VFS each keep their state in file-scope `static` variables rather
  than exported globals.

## The EL0/EL1 boundary

The kernel runs at EL1; a minimal in-kernel payload can be run at EL0, and that
transition is the kernel's first real privilege boundary. The pieces:

- **Entry.** `usermode_enter` (in `src/proc/usermode.c`, kept out of the
  do-not-touch `context.asm`) saves the kernel's callee-saved registers and SP,
  sets `ELR_EL1` to the user entry, `SP_EL0` to a user stack, `SPSR_EL1=0x3C0`
  (EL0t with DAIF masked), and `eret`s. The eret lands at EL0 purely because of
  the saved SPSR.
- **The gateway.** At EL0, `svc #0` traps into the EL1 vector table entry
  `el0_aarch64_sync` (`src/interrupts/vectors.asm`, VBAR+0x400), which decodes
  `ESR_EL1` EC=0x15, pulls the syscall number and args from the saved register
  frame, and calls the SAME `syscall_handler` the EL1 direct-call path uses. A
  non-SVC synchronous exception from EL0 (for example a trapped privileged
  instruction) takes the entry's other branch into the fault path.
- **Return.** `SYS_EXIT` from the EL0 one-shot calls `usermode_return`, which
  restores the saved kernel context and returns to the kernel caller, abandoning
  the `svc` exception frame rather than `eret`-ing back to EL0.
- **Isolation.** The user code page is mapped at VA `0x80000000` through a free
  TTBR0 L1 slot (index 2) with AP=01 (EL0 accessible) and PXN=1 (EL1 cannot
  execute it). The kernel's RAM block stays AP=00, so EL0 faults on every kernel
  address. A privileged instruction from EL0 - `msr daifset` - traps to EL1 with
  ESR EC=0x18 (because `SCTLR_EL1.UMA` is 0, the reset value the kernel never
  changes) instead of executing.

Both halves are proven headlessly by `make test`/`make audit`: `test_el0_roundtrip`
asserts the kernel regains control after the round trip and that the getpid `svc`
reached the dispatcher, and `test_el0_priv_trap` asserts the privileged-instruction
trap is EC=0x18. The production boot runs the round trip once and the serial shows
the full cycle (entered EL0 / svc N from EL0 / returned to kernel) before the
window manager loop starts.

**Honest scope.** This establishes and tests the EL0/EL1 boundary; it is NOT full
userspace memory safety. There is no per-section W^X (the kernel still runs from
one coarse RWX 1GB block, and that block is UXN=0 because `.text` shares it), and
no user-pointer validation in syscalls (the one-shot payload passes no user
pointers, so no `IS_USER_ADDR` range-check exists yet). A scheduled EL0 process
loaded from a file, W^X, and user-pointer validation are follow-on work.

## Where to read next

- `docs/` has a numbered walkthrough per subsystem.
- `README.md` covers building, running, the desktop, and the shell command set.
- Each `src/` subdirectory documents its own conventions and sharp edges.
