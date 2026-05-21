# Section 03: Interrupts and Exceptions

## Overview

This section implements the exception handling infrastructure for AEOS, including the exception vector table, GICv2 interrupt controller driver, and ARM Generic Timer.

## Status: Fully Functional

Timer interrupts work via FIQ handling. The system runs with:
- FIQ and IRQ unmasked during normal operation so the 100 Hz timer can fire
- Timer ticks at 100 Hz
- Preemptive multitasking enabled

The interrupt mask is only forced on at one point: the exception-handler entry. `handle_exception` issues `msr DAIFSet, #0xF` (debug, SError, IRQ, FIQ) before it prints anything. This is the BUG-15 fix and it is specific to the crash-dump path, not a global change to how the kernel runs.

## FIQ Solution

On QEMU virt, timer interrupts arrive as FIQ (Fast Interrupt) instead of IRQ, regardless of GIC configuration. The solution:

1. **Dedicated FIQ Handler**: `handle_fiq()` checks timer status directly
2. **Direct Timer Status Check**: Reads CNTV_CTL's ISTATUS bit to confirm timer fired
3. **FIQ Unmasking**: `interrupts_enable()` clears both FIQ and IRQ mask bits

This approach bypasses the GIC for timer interrupts, which is correct since FIQs don't go through the normal GIC acknowledge flow.

## Exception Entry: DAIF Mask

When any unhandled exception lands in `handle_exception`, the first thing it does is mask DAIF (debug, SError, IRQ, FIQ) with `msr DAIFSet, #0xF`, before the first `kprintf`. `kprintf` is not reentrant: it walks a shared crash-dump ring and prints character by character. A timer FIQ landing in the middle of that print path would corrupt the trace, so the panic path runs with interrupts masked.

This mutual exclusion holds only because the kernel is single-CPU. There is no lock inside `kprintf`; the DAIF mask is the entire guarantee. A real ring lock is future work for the SMP phase and is not present today.

## Stack-Guard Sentinel

The kernel has no MMU yet, so there is no faulting guard page below the boot stack. Instead `boot.asm` stamps a sentinel value, `STACK_GUARD_MAGIC` (0xAE057ACC), at `__stack_limit` (the bottom of the boot stack) before `kernel_main` runs. A stack overflow grows past that address and clobbers the sentinel.

Two paths check it, both in `src/kernel/stack_guard.c`:

- `handle_exception` calls `stack_guard_check(context->pc)` right after the DAIF mask, before the first `kprintf`. If the sentinel is clobbered, the panic names the offending PC (the saved `ELR_EL1`) deterministically instead of printing a misleading trace off a corrupt stack.
- `timer_handle_fiq` calls `stack_guard_check(0)` on each tick (0 because there is no faulting PC on a healthy tick). This catches a slow overflow before it reaches a fault.

On a clobbered sentinel `stack_guard_check` reports a `klog_fatal` naming the PC, masks DAIF, and halts in a `wfi` loop. The check allocates nothing and is reentrant-safe because it runs on the FIQ tick path. The PMM must not hand out the stack region or it overwrites the sentinel, so `mm.c` starts the buddy allocator at `__stack_top`, not `heap_end`.

## Components

### Exception Vector Table (vectors.asm)
- **Location**: `src/interrupts/vectors.asm`
- **Purpose**: 16-entry exception vector table for ARM64
- **Features**:
  - Context save/restore macros
  - IRQ/FIQ routing
  - Exception counters for debugging

The synchronous-exception vector exists and decodes the SVC class, but the kernel's own syscalls do not trap through it. The syscall dispatcher is a direct C function-call table lookup (see Section 05). The SVC path is reserved for future EL0 userspace work and is not the current syscall mechanism.

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

Each vector is 128 bytes. Total table size = 2KB, must be 2KB-aligned.

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

## How FIQ Handling Works

When the timer fires:

1. Timer interrupt arrives as FIQ (QEMU virt behavior)
2. FIQ vector calls `handle_fiq()`
3. `handle_fiq()` calls `timer_handle_fiq()`
4. `timer_handle_fiq()` checks CNTV_CTL's ISTATUS bit
5. If set, increments tick counter and rearms timer
6. Calls `scheduler_tick()` for preemption

The GIC is still initialized for other interrupts, but timer handling bypasses it entirely.

For interrupts that do go through the GIC, `handle_irq` and `handle_fiq` return early when `gic_acknowledge_irq()` reports one of the GICv2 spurious IDs (1020-1023, with 1022 = group-0 spurious and 1023 = group-1 spurious). Those must not be EOI'd and must never index the handler table. The threshold is the literal 1020, not `GIC_MAX_IRQ` (which is 1024, the array length); an earlier version compared against the full table size and produced thousands of "Unhandled IRQ: 1022" lines on every redraw tick.

Uptime is read separately from the tick counter. `timer_get_uptime_ms` and `timer_get_uptime_sec` read `CNTVCT_EL0` directly rather than scaling `timer.ticks`. The FIQ-driven counter falls behind wall time inside busy-wait loops (bootscreen fades, window animations), while `CNTVCT` advances regardless.

## Shell Commands

Two commands show interrupt-related information:

**uptime**: Shows system uptime and tick count
```
AEOS> uptime
System Uptime:
  Time:  0:01:23 (hh:mm:ss)
  Ticks: 8300 (at 100 Hz)
```

**irqinfo**: Shows exception vector counters (FIQ counter increases with timer ticks)
```
AEOS> irqinfo
Interrupt Statistics:

Exception Vector Counters:
  EL1 SP0: sync=0 irq=0 fiq=0 serr=0
  EL1 SPx: sync=0 irq=0 fiq=8300 serr=0
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
