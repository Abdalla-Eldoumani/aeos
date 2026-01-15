# Section 03: Interrupts and Exceptions

## Overview

This section implements the exception handling infrastructure for AEOS, including the exception vector table, GICv2 interrupt controller driver, and ARM Generic Timer. The code is complete but disabled due to FIQ routing issues on QEMU virt.

## Status: Disabled

The GIC and timer initialization code exists but is not called from `kernel_main()`. Enabling these causes FIQ exceptions instead of IRQ on QEMU virt platform. The system runs with:
- Interrupts masked (DAIF bits set)
- No timer ticks
- Cooperative multitasking only

Both physical timer (CNTP, IRQ 30) and virtual timer (CNTV, IRQ 27) were tested with the same result.

## Components

### Exception Vector Table (vectors.asm)
- **Location**: `src/interrupts/vectors.asm`
- **Purpose**: 16-entry exception vector table for ARM64
- **Features**:
  - Context save/restore macros
  - SVC (system call) handling
  - IRQ/FIQ routing
  - Exception counters for debugging

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
```

## Why Interrupts Are Disabled

When GIC and timer are initialized and interrupts enabled:

1. Timer fires as expected
2. But interrupt arrives as FIQ instead of IRQ
3. FIQ vector executes, but return causes issues

This happens with both physical timer (CNTP) and virtual timer (CNTV). The GIC is configured for Group 1 (IRQ) interrupts, but QEMU virt routes timer interrupts to FIQ regardless.

The system works fine with cooperative scheduling, so interrupts remain disabled.

## Shell Commands

Two commands show interrupt-related information even without active interrupts:

**uptime**: Shows system uptime (uses hardware counter, not ticks)
```
AEOS> uptime
System Uptime:
  Time:  0:0:0 (hh:mm:ss)
  Ticks: 0 (at 100 Hz)
```

**irqinfo**: Shows exception vector counters
```
AEOS> irqinfo
Interrupt Statistics:

Exception Vector Counters:
  EL1 SP0: sync=0 irq=0 fiq=0 serr=0
  EL1 SPx: sync=0 irq=0 fiq=0 serr=0
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
