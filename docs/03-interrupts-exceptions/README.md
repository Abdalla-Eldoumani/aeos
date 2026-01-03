# Section 03: Interrupts and Exceptions

## Overview

This section implements the exception handling infrastructure for AEOS, including the exception vector table, GICv2 interrupt controller driver, and ARM Generic Timer. Note that while the code is fully implemented, interrupts are not currently activated in the main kernel configuration due to compatibility issues.

## Status: Implemented But Not Activated

The GIC and timer initialization code exists and is complete, but it is **not called** from `kernel_main()`. The system currently runs with:
- Interrupts masked (DAIF bits set)
- No timer ticks
- Cooperative multitasking only (no preemption)

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
  - System register dumping for debugging

### GICv2 Driver (gic.c)
- **Location**: `src/interrupts/gic.c`
- **Purpose**: ARM Generic Interrupt Controller driver
- **Features**:
  - Distributor and CPU interface configuration
  - IRQ enable/disable/priority management
  - Interrupt acknowledgement and EOI
  - Software-generated interrupts (SGI)

### ARM Generic Timer (timer.c)
- **Location**: `src/interrupts/timer.c`
- **Purpose**: System timer for periodic interrupts
- **Features**:
  - 100 Hz tick rate (10ms intervals)
  - Tick counter
  - Uptime tracking
  - Busy-wait delay function

## Exception Vector Table Layout

The ARMv8 architecture defines 16 exception vectors, grouped by exception source:

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

Each vector is 128 bytes (0x80). Total table size = 2KB, must be 2KB-aligned.

## Exception Types

- **Synchronous (Sync)**: Caused by executing instruction (e.g., data abort, SVC)
- **IRQ**: Standard interrupts (timer, UART, peripherals)
- **FIQ**: Fast interrupts (higher priority, banked registers)
- **SError**: Asynchronous system errors (memory errors, bus faults)

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
- GICD_ITARGETSR: CPU targeting for SPIs

**GICC (CPU Interface)**:
- GICC_CTLR: Enable/disable CPU interface
- GICC_PMR: Priority mask
- GICC_IAR: Interrupt acknowledge
- GICC_EOIR: End of interrupt

## Timer Configuration

- **Frequency**: Determined by CNTFRQ_EL0 register (typically 62.5 MHz on QEMU)
- **Tick Rate**: 100 Hz (TIMER_FREQ_HZ)
- **IRQ Number**: 30 (physical timer interrupt)
- **Registers**:
  - CNTP_CTL_EL0: Timer control (enable/disable)
  - CNTP_TVAL_EL0: Timer value (countdown)
  - CNTPCT_EL0: Current counter value

## API Reference

### Interrupt Control

```c
/* Enable/disable IRQ interrupts (assembly functions) */
void interrupts_enable(void);
void interrupts_disable(void);

/* Initialize interrupt subsystem */
void interrupts_init(void);

/* Register IRQ handler */
void irq_register_handler(uint32_t irq, irq_handler_t handler);

/* Unregister IRQ handler */
void irq_unregister_handler(uint32_t irq);
```

### GIC Functions

```c
/* Initialize GIC */
void gic_init(void);

/* Enable/disable specific IRQ */
void gic_enable_irq(uint32_t irq);
void gic_disable_irq(uint32_t irq);

/* Set IRQ priority (0 = highest, 255 = lowest) */
void gic_set_priority(uint32_t irq, uint8_t priority);

/* Acknowledge interrupt (returns IRQ number) */
uint32_t gic_acknowledge_irq(void);

/* Signal end of interrupt */
void gic_end_of_irq(uint32_t irq);

/* Send software interrupt */
void gic_send_sgi(uint32_t sgi_num, uint32_t target_cpu);
```

### Timer Functions

```c
/* Initialize timer (doesn't start it) */
void timer_init(void);

/* Start the timer */
void timer_start(void);

/* Get tick count */
uint64_t timer_get_ticks(void);

/* Get uptime in milliseconds */
uint64_t timer_get_uptime_ms(void);

/* Get uptime in seconds */
uint64_t timer_get_uptime_sec(void);

/* Busy-wait delay (uses counter, works without interrupts) */
void timer_delay_ms(uint32_t ms);
```

## How to Activate Interrupts

To enable interrupt support, add this to `kernel_main()`:

```c
/* After other initialization */
interrupts_init();      /* Install vector table */
gic_init();             /* Initialize GIC */
timer_init();           /* Initialize timer (doesn't start) */
interrupts_enable();    /* Unmask IRQs */
timer_start();          /* Start timer ticks */
```

**Warning**: This is untested in the current configuration and may require debugging.

## Context Switching

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
    sub sp, sp, #272            /* Make space on stack */
    stp x0, x1, [sp, #(16 * 0)] /* Save x0-x1 */
    stp x2, x3, [sp, #(16 * 1)] /* Save x2-x3 */
    /* ... save all registers ... */
    mrs x0, elr_el1             /* Save PC */
    mrs x1, spsr_el1            /* Save PSTATE */
    stp x0, x1, [sp, #256]
.endm
```

### RESTORE_CONTEXT Macro

Reverses the save process, then adjusts SP.

## SVC (System Call) Handling

The SPx sync vector checks ESR_EL1 for exception class:

```assembly
    mrs x0, esr_el1
    lsr x1, x0, #26         /* Extract EC bits [31:26] */
    cmp x1, #0x15           /* EC = 0x15 means SVC */
    bne 1f                  /* Not SVC, generic handler */

    /* This is an SVC - call syscall_handler */
    ldr x0, [sp, #(16 * 4)] /* x8 = syscall number */
    ldr x1, [sp, #(16 * 0)] /* x0 = arg0 */
    /* ... load other args ... */
    bl syscall_handler
    str x0, [sp, #(16 * 0)] /* Store return value */
```

## Known Issues

### SVC Routing Bug (Workaround Present)

On some configurations, SVC instructions are incorrectly routed to the FIQ vector instead of sync. The code includes a workaround in `el1_sp0_fiq` that checks ESR_EL1 and handles SVCs properly.

### Interrupts Not Enabled

The main kernel doesn't call the interrupt initialization functions. This is intentional - the code exists for future use.

### No Preemptive Multitasking

Without timer interrupts, the scheduler is purely cooperative. Processes must call `yield()` voluntarily.

### SP0 vs SPx

AEOS uses SP_EL1 (SPx mode), so exceptions should hit the SPx vectors (0x200-0x380). The SP0 vectors (0x000-0x180) are included for completeness but shouldn't be used.

## Testing (When Activated)

### Test Exception Handling

```c
/* Trigger data abort */
*(volatile uint32_t *)0xDEADBEEF = 42;  /* Should catch and print exception */
```

### Test Timer Interrupts

```c
timer_init();
timer_start();
interrupts_enable();

/* Wait for ticks */
while (timer_get_ticks() < 100) {
    /* Should see tick count increment */
}
```

### Test IRQ Registration

```c
void my_handler(void) {
    kprintf("IRQ fired!\n");
}

irq_register_handler(30, my_handler);  /* Timer IRQ */
```

## Debug Counters

The vector table includes counters for each exception type:

```c
extern uint64_t exception_counters[16];

/* Check if exceptions occurred */
kprintf("SPx sync: %llu\n", exception_counters[4]);
kprintf("SPx IRQ: %llu\n", exception_counters[5]);
```