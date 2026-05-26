# Section 03: Interrupts and Exceptions

## Overview

This section implements the exception handling infrastructure for AEOS, including the exception vector table, GICv2 interrupt controller driver, and ARM Generic Timer.

## Status: Fully Functional

Timer interrupts work via the IRQ path. The system runs with:
- FIQ and IRQ unmasked during normal operation so the 100 Hz timer can fire
- Timer ticks at 100 Hz
- Preemptive multitasking enabled

The interrupt mask is only forced on at one point: the exception-handler entry. `handle_exception` issues `msr DAIFSet, #0xF` (debug, SError, IRQ, FIQ) before it prints anything. This is the BUG-15 fix and it is specific to the crash-dump path, not a global change to how the kernel runs.

## Timer Interrupt Delivery

On QEMU virt the virtual-timer PPI (INTID 27) is delivered as an **IRQ** and serviced through the normal GIC acknowledge flow:

1. **Group 0 placement**: `gic_init` moves INTID 27 into GIC Group 0. With `GICC_CTLR.FIQEn = 0`, a Group 0 interrupt is signaled as IRQ (not FIQ).
2. **Plain IAR acknowledge**: the IRQ arrives on the `el1_spx_irq -> handle_irq` path, which reads `GICC_IAR` and gets the real INTID 27, dispatches the registered `timer_irq_handler` (which re-arms `CNTV_TVAL` to clear the condition), then writes `GICC_EOIR`.

Why Group 0 and not Group 1: QEMU virt's GICv2 has no security extensions. With the recommended `AckCtl = 0` model, a Group 1 interrupt makes a `GICC_IAR` read return the spurious value 1022 and must be acknowledged via the aliased `GICC_AIAR` register -- but without the Non-secure register banking, an `AIAR` read returns 0, so a Group 1 timer is never acknowledged and the interrupt storms. Group 0 + `FIQEn = 0` keeps the timer on the IRQ path while making `GICC_IAR` return its real INTID.

Historical note: earlier revisions of this kernel claimed the timer arrived as FIQ "regardless of GIC configuration" and that the FIQ path bypassed the GIC. That was an artifact of an oversized, misaligned vector table: the EL1h IRQ slot (VBAR+0x280) physically fell inside the SP0 FIQ handler body, so the timer was serviced by the FIQ fragment by accident. Once the table was realigned to canonical 0x80 branch stubs, the timer had to be made a real, serviceable IRQ. `handle_fiq` / `timer_handle_fiq` remain as a dormant fallback.

## Exception Entry: DAIF Mask

When any unhandled exception lands in `handle_exception`, the first thing it does is mask DAIF (debug, SError, IRQ, FIQ) with `msr DAIFSet, #0xF`, before the first `kprintf`. `kprintf` is not reentrant: it walks a shared crash-dump ring and prints character by character. A timer FIQ landing in the middle of that print path would corrupt the trace, so the panic path runs with interrupts masked.

The DAIF mask is the same-core exclusion: it stops a timer IRQ on this core from preempting a ring write while the dump reads the buffer. Under SMP it does nothing about the other cores, so `kprintf` also carries a dedicated ring spinlock (`kprintf_ring_lock`, kprintf.c). `putchar` takes it across the ring store and the index/wrap update so two cores cannot splice a torn `(pos, wrapped)` pair. The lock is additive: it adds the cross-core ordering on top of the DAIF mask, it does not replace it (this is the SMP-phase ring lock the earlier single-CPU note deferred).

The panic reader does not block on that lock. `kprintf_ring_walk` uses `spin_trylock`-or-bypass: it tries the lock and reads the ring whether or not it gets it. A faulted core could hold the lock forever, and a blocking acquire there would deadlock the panic so the crash dump prints nothing - so the dump always reads, accepting a slightly-torn read over a silent panic. The same short lock window in `putchar` keeps the same-core sequence (`handle_exception` -> `kprintf` -> `putchar` lock/unlock, then `kprintf_ring_walk`) from self-deadlocking on the non-recursive lock.

## DWARF file:line backtrace

The backtrace on the panic path (reached from `handle_exception` through `crash_dump_save` in `backtrace.c`) resolves each saved link register to a function name through the symbol table (`scripts/gen-symbols.sh` -> `aeos_symbols[]`). It also resolves the address to a source `file:line` through a second embedded table, the addr->file:line sibling of the symbol table.

That table cannot be read from the ELF at runtime. `.debug_line` has VMA 0 and sits past the single PT_LOAD extent, so `qemu -kernel kernel.elf` loads none of it; an in-kernel parser would read zeros. So the table is extracted at build time: `scripts/gen-lines.py` runs `readelf --debug-dump=decodedline` on `kernel-stage1.elf`, sorts the rows by address, collapses consecutive same-(file,line) runs, and emits a packed `aeos_lines[]` (an 8-byte `{addr_off, file_off, line}` entry plus a NUL-separated file-name pool) as a `.rodata` translation unit. The table IS the decoded output of the toolchain's own DWARF line program. It rides the existing symbol-table two-pass and lives in `.rodata` (after `.text` in `linker.ld`), so growing it by ~137 KB never shifts a function address - the same stability the symbol table relies on. This plan provides the embedded table; the per-frame `(file:line)` print is added alongside the symbol lookup in a follow-on plan.

## Stack-Guard Sentinel

The kernel has no MMU yet, so there is no faulting guard page below the boot stack. Instead `boot.asm` stamps a sentinel value, `STACK_GUARD_MAGIC` (0xAE057ACC), at `__stack_limit` (the bottom of the boot stack) before `kernel_main` runs. A stack overflow grows past that address and clobbers the sentinel.

Two paths check it, both in `src/kernel/stack_guard.c`:

- `handle_exception` calls `stack_guard_check(context->pc)` right after the DAIF mask, before the first `kprintf`. If the sentinel is clobbered, the panic names the offending PC (the saved `ELR_EL1`) deterministically instead of printing a misleading trace off a corrupt stack.
- `timer_handle_fiq` calls `stack_guard_check(0)` on each tick (0 because there is no faulting PC on a healthy tick). This catches a slow overflow before it reaches a fault.

On a clobbered sentinel `stack_guard_check` reports a `klog_fatal` naming the PC, masks DAIF, and halts in a `wfi` loop. The check allocates nothing and is reentrant-safe because it runs on the timer tick path (an IRQ on this setup). The PMM must not hand out the stack region or it overwrites the sentinel, so `mm.c` starts the buddy allocator at `__stack_top`, not `heap_end`.

## Components

### Exception Vector Table (vectors.asm)
- **Location**: `src/interrupts/vectors.asm`
- **Purpose**: 16-entry exception vector table for ARM64
- **Features**:
  - Context save/restore macros
  - IRQ/FIQ routing
  - Exception counters for debugging

The kernel's own syscalls do not trap through the synchronous vector - they are direct C function-call table lookups (see Section 05). But the EL0 synchronous vector entry (`el0_aarch64_sync`, VBAR+0x400) DOES decode `svc` from EL0 and route it through the same dispatcher: this is the Phase 5 privilege boundary. Its non-SVC branch handles trapped privileged instructions from EL0 (for example `msr daifset`, which faults with EC=0x18) via a seam that records the fault for tests and otherwise reports it through the normal halting path.

### Exception Handlers (exceptions.c)
- **Location**: `src/interrupts/exceptions.c`
- **Purpose**: C-level exception handling
- **Features**:
  - Generic exception handler with diagnostic output
  - IRQ dispatcher
  - Handler registration
  - System register dumping

### GICv2 Driver (gic.c)
- **Location**: `src/interrupts/gic.c`
- **Purpose**: ARM Generic Interrupt Controller driver
- **Features**:
  - Distributor and CPU interface configuration
  - IRQ enable/disable/priority management
  - Interrupt acknowledgement and EOI

### ARM Generic Timer (timer.c)
- **Location**: `src/interrupts/timer.c`
- **Purpose**: System timer for periodic interrupts
- **Features**:
  - 100 Hz tick rate (10ms intervals)
  - Tick counter and uptime tracking
  - Busy-wait delay function (works without interrupts)

## Exception Vector Table Layout

ARMv8 defines 16 exception vectors grouped by source:

```
Offset   Source                    Type
------   ----------------------    --------
0x000    Current EL with SP0       Sync
0x080    Current EL with SP0       IRQ
0x100    Current EL with SP0       FIQ
0x180    Current EL with SP0       SError

0x200    Current EL with SPx       Sync
0x280    Current EL with SPx       IRQ
0x300    Current EL with SPx       FIQ
0x380    Current EL with SPx       SError

0x400    Lower EL (AArch64)        Sync
0x480    Lower EL (AArch64)        IRQ
0x500    Lower EL (AArch64)        FIQ
0x580    Lower EL (AArch64)        SError

0x600    Lower EL (AArch32)        Sync
0x680    Lower EL (AArch32)        IRQ
0x700    Lower EL (AArch32)        FIQ
0x780    Lower EL (AArch32)        SError
```

Each vector slot is 128 bytes (0x80); total table size = 2KB, must be 2KB-aligned. Each slot holds a single `b <handler>` branch stub, and the handler bodies live in a block below the 2KB table. Keeping each slot to one branch is what guarantees the offsets above line up; an inlined body that exceeds 0x80 shifts every later slot and misaligns the table.

## GICv2 Memory Map (QEMU virt)

```
0x08000000: GIC Distributor (GICD)
0x08010000: GIC CPU Interface (GICC)
```

### Key Registers

**GICD (Distributor)**:
- GICD_CTLR: Enable/disable distributor
- GICD_IGROUPR: Group 0 (FIQ) vs Group 1 (IRQ)
- GICD_ISENABLER: Enable interrupts
- GICD_IPRIORITYR: Interrupt priorities

**GICC (CPU Interface)**:
- GICC_CTLR: Enable/disable CPU interface
- GICC_PMR: Priority mask
- GICC_IAR: Interrupt acknowledge
- GICC_EOIR: End of interrupt

## Timer Configuration

- **Frequency**: From CNTFRQ_EL0 (typically 62.5 MHz on QEMU)
- **Tick Rate**: 100 Hz (TIMER_FREQ_HZ)
- **Virtual Timer IRQ**: 27
- **Physical Timer IRQ**: 30

## API Reference

### Interrupt Control

```c
/* Enable/disable IRQ interrupts */
void interrupts_enable(void);
void interrupts_disable(void);

/* Initialize interrupt subsystem */
void interrupts_init(void);

/* Register IRQ handler */
void irq_register_handler(uint32_t irq, irq_handler_t handler);
```

### GIC Functions

```c
/* Initialize GIC */
void gic_init(void);

/* Enable/disable specific IRQ */
void gic_enable_irq(uint32_t irq);
void gic_disable_irq(uint32_t irq);

/* Set IRQ priority */
void gic_set_priority(uint32_t irq, uint8_t priority);

/* Acknowledge and end interrupt */
uint32_t gic_acknowledge_irq(void);
void gic_end_of_irq(uint32_t irq);
```

### Timer Functions

```c
/* Initialize and start timer */
void timer_init(void);
void timer_start(void);

/* Get tick count and uptime */
uint64_t timer_get_ticks(void);
uint64_t timer_get_uptime_ms(void);
uint64_t timer_get_uptime_sec(void);

/* Busy-wait delay (works without interrupts) */
void timer_delay_ms(uint32_t ms);

/* Handle timer interrupt from FIQ (returns true if handled) */
bool timer_handle_fiq(void);
```

## How Timer Handling Works

When the timer fires:

1. Timer interrupt arrives as an IRQ (Group 0, `FIQEn = 0`; see "Timer Interrupt Delivery")
2. The `el1_spx_irq` stub calls `handle_irq()`
3. `handle_irq()` reads `GICC_IAR`, gets the real INTID 27, and calls the registered `timer_irq_handler()`
4. `timer_irq_handler()` re-arms `CNTV_TVAL` (clearing the timer condition) and increments the tick counter
5. Calls `scheduler_tick()` for preemption
6. `handle_irq()` writes `GICC_EOIR` to complete the interrupt

The timer goes through the normal GIC acknowledge flow like any other IRQ. The dormant `handle_fiq` / `timer_handle_fiq` path (which reads CNTV_CTL's ISTATUS bit directly) is kept only as a fallback.

`handle_irq` and `handle_fiq` return early when `gic_acknowledge_irq()` reports one of the GICv2 spurious IDs (1020-1023, with 1022 = group-0 spurious and 1023 = group-1 spurious). Those must not be EOI'd and must never index the handler table. The threshold is the literal 1020, not `GIC_MAX_IRQ` (which is 1024, the array length); an earlier version compared against the full table size and produced thousands of "Unhandled IRQ: 1022" lines on every redraw tick. (This same 1022 value is also what a `GICC_IAR` read returns for a Group 1 interrupt -- the reason the timer is placed in Group 0.)

Uptime is read separately from the tick counter. `timer_get_uptime_ms` and `timer_get_uptime_sec` read `CNTVCT_EL0` directly rather than scaling `timer.ticks`. The interrupt-driven counter falls behind wall time inside busy-wait loops (bootscreen fades, window animations), while `CNTVCT` advances regardless.

## Shell Commands

Two commands show interrupt-related information:

**uptime**: Shows system uptime and tick count
```
AEOS> uptime
System Uptime:
  Time:  0:01:23 (hh:mm:ss)
  Ticks: 8300 (at 100 Hz)
```

**irqinfo**: Shows exception vector counters (the EL1 SPx IRQ counter increases with timer ticks)
```
AEOS> irqinfo
Interrupt Statistics:

Exception Vector Counters:
  EL1 SP0: sync=0 irq=0 fiq=0 serr=0
  EL1 SPx: sync=0 irq=8300 fiq=0 serr=0
  ...
```

## Context Save/Restore

### Saved Context (272 bytes)

```c
typedef struct {
    uint64_t x0-x30;    /* General purpose registers */
    uint64_t sp;        /* Stack pointer */
    uint64_t pc;        /* Program counter (ELR_EL1) */
    uint64_t pstate;    /* Processor state (SPSR_EL1) */
} cpu_context_t;
```

### SAVE_CONTEXT Macro

```assembly
.macro SAVE_CONTEXT
    sub sp, sp, #272
    stp x0, x1, [sp, #(16 * 0)]
    stp x2, x3, [sp, #(16 * 1)]
    /* ... save all registers ... */
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #256]
.endm
```

## Debug Counters

The vector table includes counters for each exception type:

```c
extern uint64_t exception_counters[16];

/* Check if exceptions occurred */
kprintf("SPx sync: %llu\n", exception_counters[4]);
kprintf("SPx IRQ: %llu\n", exception_counters[5]);
kprintf("SPx FIQ: %llu\n", exception_counters[6]);
```

These are displayed by the `irqinfo` shell command.
