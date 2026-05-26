# AEOS Architecture

This document is the one-page mental model of how AEOS boots and how data moves
through it. Read it before diving into a subsystem; the per-section walkthroughs
in `docs/` go deeper.

It describes the kernel as it stands today: a single-address-space AArch64 kernel.
The MMU is enabled with an identity map plus a high-half alias (described below).
There is no general userspace and no SMP or networking, and the kernel does not
yet enforce per-section W^X for its own image. The EL0/EL1 privilege boundary,
however, is established, tested, and exercised by a real program: a static ELF64
is loaded from a file, mapped with per-segment W^X, and run at EL0, reaching the
kernel only through trapped `svc` syscalls; a privileged instruction from EL0
faults to EL1; the loaded process appears in `ps` and is reaped by `kill` (see the
EL0/EL1 boundary and ELF-loader sections). SMP/preemption - and with it interactive
mid-run kill from a concurrent prompt - is the next milestone.

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
8. `process_init` then `scheduler_init`. Then `smp_init` brings up secondary cores
   1 through 3 via PSCI CPU_ON with a bounded handshake; a stuck or failed
   secondary is logged and skipped so the primary always proceeds. The serial
   shows `smp: core N online` per secondary and `smp: N cores online`.
9. `syscall_init` to populate the syscall table, then `virtio_net_init` (probes the
   virtio-net device and prints its MAC, no-ops if the device is absent) and
   `pl031_init` (reads the PL031 RTC and logs the wall-clock time). Then two
   one-shot EL0 runs (both before the shell, each run once, never looped). First a
   round trip (`usermode_run_payload`) that drops to EL0, issues `svc #0` for getpid
   and exit, and returns to the kernel - the serial shows "entered EL0", "svc N from
   EL0", "returned to kernel"; SPSR=0x3C0 masks IRQ/FIQ so the running timer cannot
   preempt the brief EL0 lifetime. Then the embedded test ELF is written to ramfs
   `/hello` and `elf_exec_file("/hello")` loads and runs it at EL0 - the serial
   shows the loader mapping it and the binary printing "hello, EL0!". A failed load
   is logged and ignored so boot always continues. Last, when a net device is
   present, a one-shot `ping 10.0.2.2` demo sends one ICMP echo to the slirp gateway
   and logs the reply or a timeout; the bounded `net_ping` never hangs the boot, and
   an absent device just logs that the demo was skipped.
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
6. Back in the loop, the frame tick forces a repaint every ~33 ms (30 FPS):
   `wm_run` sets the redraw flag on each tick, so `wm_update_display` repaints
   through `wm_redraw` whether or not an event arrived - this is what keeps the
   wall clock and the open/close animations advancing without input. An event
   that set the flag between ticks repaints immediately, so a click is
   sub-frame responsive. `wm_redraw` runs every visible window's `on_paint`,
   composites any active toast notifications above the windows, draws the
   software mouse cursor, and flushes the framebuffer to the GPU.

Because the loop repaints every frame, live-content apps such as the system
monitor graph and the notes caret advance on their own; they may also call
`wm_request_redraw` directly, which is now redundant but harmless.

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

## Data flow three: a ping to an ICMP echo reply

This is what happens when a user runs `ping 10.0.2.2` (or the one-shot boot demo
runs it).

1. The shell dispatches `ping` to `cmd_ping` (`src/kernel/shell.c`), which parses
   the dotted-quad argument and calls `net_ping` (`src/net/net.c`).
2. `net_ping` resolves the gateway MAC with `arp_resolve` if needed, records the
   awaited ICMP id and sequence under the driver's `net_lock`, releases the lock,
   and sends the echo request with `icmp_send_echo`. The send goes out through
   `net_tx` (`src/drivers/virtio_net.c`), which posts the frame on the TX queue.
3. The poll loop calls `net_rx_poll`, which pulls a received frame off the RX
   queue, and hands it to `net_rx_dispatch`. That parser bounds-checks every layer
   (Ethernet, ARP or IPv4, ICMP) before indexing and drops on any inconsistency.
4. On a type-0 ICMP reply whose id and sequence match the awaited pair, the pending
   state flips its `got_reply` flag under `net_lock`, and `net_ping` returns success
   to the shell, which prints the reply line. Both `arp_resolve` and `net_ping`
   poll with a bound, so neither ever hangs the prompt.

The driver is poll-driven, not interrupt-driven, so this whole path runs on the
calling thread and the same code serves the production boot and the headless test
runner (which has no GIC or timer).

The taskbar reads the PL031 RTC directly: `desktop_draw_taskbar`
(`src/kernel/desktop.c`) reads `RTC_DR` through the identity-mapped Device block
each frame and formats it with `pl031_format_hms`, so the clock is real
wall-clock time rather than an uptime counter.

## Constraints

These follow from the properties above and are easy to violate by accident:

- **One scheduling CPU.** Secondary cores come online via PSCI but park in `wfe`;
  only the primary runs scheduled work, so there is no cross-core preemption. The
  shared state that two cores could touch is now lock-protected rather than relying
  on a single-CPU assumption: the scheduler runqueue takes `scheduler_lock`, the
  net RX/TX path takes `net_lock`, and the kprintf crash-dump ring takes
  `kprintf_ring_lock` (additive to the exception handler's DAIF mask, which remains
  the same-core exclusion). Cross-core preemptive scheduling is the next milestone.
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
- **Identity mapping, no demand paging.** The MMU is enabled but the
  kernel runs through an identity map, so virtual equals physical and there is no
  copy-on-write and no `mmap`. The whole KERNEL (code, rodata, data, heap, stack)
  lives in one RWX 1GB block, so there is no per-section W^X for the kernel yet - a
  later milestone once finer-grained kernel tables land. LOADED EL0 code, by
  contrast, does get per-segment W^X (executable segments are mapped read-only).
  User-pointer validation now exists for `sys_write` (the EL0 buffer is range-checked
  against the mapped window), but it is not yet a general `copy_from_user` covering
  every syscall. The heap is 4 MB and its base moves with the kernel image size.
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

## Loading a real ELF at EL0

The same EL0 mechanism now runs a real program loaded from a file. `elf_exec_file`
(`src/proc/exec.c`) parses a static ELF64 off the VFS, validates the header
(`elf_validate`, the reject-never-fault gate), and maps each `PT_LOAD` segment
into the `0x80000000` user window with **per-segment W^X**: an executable segment
(`PF_X`) is mapped read-only-executable (USER_TEXT, AP=11, so EL0 cannot rewrite
its own code), data read-write-no-execute (USER_DATA). The BSS tail is zero-filled,
a fresh EL0 stack is mapped above the top segment, and the program is entered at
`e_entry` via the same proven one-shot. `sys_write` from EL0 bound-checks the user
buffer against the mapped window before the kernel touches a byte.

The loaded program is **registered** in a scheduler-independent process registry,
so `ps` lists its pid, name, and state alongside the kernel threads. The `kill`
command calls `process_kill(pid)`, which sets a flag on the registered process; the
syscall handler honors that flag at the program's next `svc` boundary and reaps it
(returns to the kernel) instead of resuming EL0. The production boot writes the
embedded test binary to `/hello` and runs it once: the serial shows the loader
mapping it, the binary printing "hello, EL0!" through `sys_write`, and control
returning to the kernel before the window manager loop starts.

**Honest scope.** Static ELF only - no dynamic linking, no relocations (locked: the
loader rejects `ET_DYN`/PIE). One EL0 program runs at a time, in a single static
user window. Because the loader runs the program SYNCHRONOUSLY to completion, the
`kill` command reaps a REGISTERED process by pid (the flag is honored at the next
`svc`, proven headlessly), but INTERACTIVE mid-run kill from a second live prompt
while a program is executing is NOT yet delivered - there is no concurrent prompt,
and that needs the SMP/preemption work of a later phase. So "a real userspace ELF
runs at EL0, is visible in `ps`, and is reaped by `kill`" is accurate today;
"interactive mid-run kill from a second prompt" is not. Per-section W^X for the
KERNEL (it still runs from one coarse RWX 1GB block) and a general `copy_from_user`
for every future syscall (only `sys_write` is covered) remain follow-on work.

## Where to read next

- `docs/` has a numbered walkthrough per subsystem.
- `README.md` covers building, running, the desktop, and the shell command set.
- Each `src/` subdirectory documents its own conventions and sharp edges.
