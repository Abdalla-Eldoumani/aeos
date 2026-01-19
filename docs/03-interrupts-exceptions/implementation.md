# Interrupts and Exceptions - Implementation Details

## vectors.asm - Exception Vector Table

### Table Alignment

```assembly
    .section .text.vectors
    .balign 2048                    /* Must be 2KB-aligned */
    .global exception_vector_table
exception_vector_table:
```

The vector table must be aligned to 2KB (0x800) per ARMv8 specification. The `.balign 2048` directive ensures this.

### Vector Entry Layout

Each of the 16 vectors is exactly 128 bytes (0x80):

```assembly
    .balign 128                     /* Start of vector */
el1_spx_irq:
    SAVE_CONTEXT
    /* ... handle IRQ ... */
    RESTORE_CONTEXT
    eret
    /* Padding to 128 bytes if needed */
```

### Context Size Calculation

```assembly
.set CONTEXT_SIZE, 272   /* 34 registers * 8 bytes */
```

**Registers saved**:
- x0-x30 (31 registers = 248 bytes)
- SP (8 bytes)
- PC/ELR_EL1 (8 bytes)
- PSTATE/SPSR_EL1 (8 bytes)
- Total: 272 bytes

### SAVE_CONTEXT Implementation

```assembly
.macro SAVE_CONTEXT
    sub sp, sp, #CONTEXT_SIZE       /* Allocate stack space */

    /* Save GPRs */
    stp x0, x1, [sp, #(16 * 0)]
    stp x2, x3, [sp, #(16 * 1)]
    /* ... */
    stp x28, x29, [sp, #(16 * 14)]

    /* Save x30 (LR) */
    str x30, [sp, #240]

    /* Save SP value (before we subtracted CONTEXT_SIZE) */
    add x0, sp, #CONTEXT_SIZE
    str x0, [sp, #248]

    /* Save PC and PSTATE */
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #256]
.endm
```

**Stack layout after SAVE_CONTEXT**:
```
SP+256: PC, PSTATE
SP+248: SP (original value)
SP+240: x30
SP+0:   x0-x29 (pairs)
```

### SVC Detection and Handling

```assembly
el1_spx_sync:
    SAVE_CONTEXT

    /* Check ESR_EL1 for exception class */
    mrs x0, esr_el1
    lsr x1, x0, #26             /* EC = bits [31:26] */
    cmp x1, #0x15               /* 0x15 = SVC from AArch64 */
    bne 1f                      /* Not SVC, handle generically */

    /* Extract syscall arguments from saved context */
    ldr x0, [sp, #(16 * 4)]     /* x8 from saved context */
    ldr x1, [sp, #(16 * 0)]     /* x0 */
    ldr x2, [sp, #(16 * 0 + 8)] /* x1 */
    ldr x3, [sp, #(16 * 1)]     /* x2 */
    ldr x4, [sp, #(16 * 1 + 8)] /* x3 */
    ldr x5, [sp, #(16 * 2)]     /* x4 */
    ldr x6, [sp, #(16 * 2 + 8)] /* x5 */

    bl syscall_handler

    /* Store return value back to saved x0 */
    str x0, [sp, #(16 * 0)]

    RESTORE_CONTEXT
    eret
```

**Why load from stack?** The saved context contains the register values at the time of the SVC instruction. We need to extract arguments from there, not from current registers.

### Exception Counters

```assembly
    .section .data
    .global exception_counters
exception_counters:
    .quad 0  /*  0: el1_sp0_sync */
    .quad 0  /*  1: el1_sp0_irq */
    /* ... 14 more ... */
```

Each vector increments its counter:

```assembly
el1_spx_sync:
    SAVE_CONTEXT

    /* Increment counter */
    stp x0, x1, [sp, #-16]!         /* Save x0, x1 */
    adr x0, exception_counters
    ldr x1, [x0, #32]               /* Load counter[4] */
    add x1, x1, #1
    str x1, [x0, #32]               /* Store back */
    ldp x0, x1, [sp], #16           /* Restore x0, x1 */
```

**Counter index**: SP0 sync=0, SPx sync=4, SPx IRQ=5, etc.

### Interrupt Enable/Disable

```assembly
    .global interrupts_enable
interrupts_enable:
    msr daifclr, #3     /* Clear FIQ and IRQ mask bits (F=0, I=0) */
    isb
    ret

    .global interrupts_disable
interrupts_disable:
    msr daifset, #3     /* Set FIQ and IRQ mask bits (F=1, I=1) */
    isb
    ret
```

**DAIF register**:
- D: Debug exceptions (bit 3)
- A: SError (async abort) (bit 2)
- I: IRQ (bit 1)
- F: FIQ (bit 0)

We use `#3` to enable both FIQ and IRQ since timer interrupts arrive as FIQ on QEMU virt.

## exceptions.c - Exception Handling

### ESR_EL1 Decoding

```c
static uint32_t get_exception_class(uint64_t esr)
{
    return (esr >> 26) & 0x3F;  /* EC = bits [31:26] */
}
```

**Exception Classes**:
- 0x00: Unknown
- 0x15: SVC instruction
- 0x20/0x21: Instruction abort
- 0x24/0x25: Data abort
- 0x26: SP alignment fault

### Generic Exception Handler

```c
void handle_exception(uint32_t source, uint32_t type, cpu_context_t *context)
{
    uint64_t esr = get_exception_syndrome();
    uint64_t far = get_fault_address();
    uint32_t ec = get_exception_class(esr);

    kprintf("\n======== EXCEPTION ========\n");
    kprintf("Type: %s\n", exception_type_name(type));
    kprintf("Source: %s\n", source_name(source));
    kprintf("Class: ");
    print_exception_class(ec);
    kprintf("\nPC:     %p\n", (void *)context->pc);
    kprintf("SP:     %p\n", (void *)context->sp);
    kprintf("ESR:    %p\n", (void *)esr);
    kprintf("FAR:    %p\n", (void *)far);

    /* Halt system */
    while (1) {
        __asm__ volatile("wfi");
    }
}
```

**ESR_EL1**: Exception Syndrome Register - describes what went wrong
**FAR_EL1**: Fault Address Register - virtual address that caused fault

### IRQ Handler

```c
void handle_irq(uint32_t source, uint32_t type, cpu_context_t *context)
{
    /* Acknowledge interrupt and get IRQ number */
    uint32_t irq = gic_acknowledge_irq();

    /* Call registered handler */
    if (irq < GIC_MAX_IRQ && irq_handlers[irq] != NULL) {
        irq_handlers[irq]();
    }

    /* Signal end of interrupt */
    gic_end_of_irq(irq);
}
```

**Flow**:
1. Read GICC_IAR to get IRQ number and acknowledge
2. Call handler function
3. Write to GICC_EOIR to signal completion

### FIQ Handler

```c
void handle_fiq(uint32_t source, uint32_t type, cpu_context_t *context)
{
    (void)source; (void)type; (void)context;
    exception_stats.fiq_count++;

    /* First try timer - most common FIQ source */
    if (timer_handle_fiq()) {
        return;
    }

    /* Fallback: try GIC acknowledge for other FIQ sources */
    uint32_t irq = gic_acknowledge_irq();
    if (irq < GIC_MAX_IRQ) {
        if (irq_handlers[irq]) {
            irq_handlers[irq]();
        }
        gic_end_of_irq(irq);
    }
}
```

**Why direct timer check?** On QEMU virt, timer interrupts arrive as FIQ regardless of GIC group configuration. The GIC doesn't track FIQs, so `gic_acknowledge_irq()` returns spurious (1022). We check the timer's ISTATUS bit directly instead.

### Timer FIQ Handler

```c
bool timer_handle_fiq(void)
{
    uint32_t ctl;

    if (!timer.initialized) {
        return false;
    }

    /* Read timer control register */
    ctl = read_cntv_ctl();

    /* Check ISTATUS bit (bit 2) - timer interrupt pending */
    if (!(ctl & (1 << 2))) {
        return false;  /* Not a timer interrupt */
    }

    /* Handle the timer tick */
    timer.ticks++;

    /* Rearm timer for next tick */
    write_cntv_tval(timer.tick_interval);

    /* Call scheduler for potential preemption */
    scheduler_tick();

    return true;
}
```

**CNTV_CTL bits**:
- Bit 0 (ENABLE): Timer enabled
- Bit 1 (IMASK): Interrupt masked
- Bit 2 (ISTATUS): Interrupt pending (read-only)

## gic.c - GICv2 Driver

### Initialization Sequence

```c
void gic_init(void)
{
    /* Get number of IRQs */
    uint32_t num_irqs = ((MMIO_READ(GICD_TYPER) & 0x1F) + 1) * 32;

    /* Disable distributor */
    MMIO_WRITE(GICD_CTLR, 0);

    /* Disable all interrupts */
    for (i = 0; i < num_irqs; i += 32) {
        MMIO_WRITE(GICD_ICENABLER + (i / 32) * 4, 0xFFFFFFFF);
    }

    /* Set all to Group 1 (IRQ) */
    for (i = 0; i < num_irqs; i += 32) {
        MMIO_WRITE(GICD_IGROUPR + (i / 32) * 4, 0xFFFFFFFF);
    }

    /* Set priorities to lowest */
    for (i = 0; i < num_irqs; i += 4) {
        MMIO_WRITE(GICD_IPRIORITYR + i, 0xA0A0A0A0);
    }

    /* Enable distributor for Group 1 */
    MMIO_WRITE(GICD_CTLR, GICD_CTLR_ENABLE_GRP1);

    /* Configure CPU interface */
    MMIO_WRITE(GICC_PMR, 0xFF);     /* Allow all priorities */
    MMIO_WRITE(GICC_BPR, 0);        /* No priority grouping */
    MMIO_WRITE(GICC_CTLR, GICC_CTLR_ENABLE_GRP1);
}
```

### Enable/Disable IRQ

```c
void gic_enable_irq(uint32_t irq)
{
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;

    MMIO_WRITE(GICD_ISENABLER + reg * 4, (1 << bit));
}
```

**Register layout**: Each 32-bit register controls 32 IRQs (1 bit per IRQ).

### Priority Setting

```c
void gic_set_priority(uint32_t irq, uint8_t priority)
{
    uint32_t reg = irq / 4;
    uint32_t shift = (irq % 4) * 8;
    uint32_t val;

    /* Read-modify-write */
    val = MMIO_READ(GICD_IPRIORITYR + reg * 4);
    val &= ~(0xFF << shift);
    val |= (priority << shift);
    MMIO_WRITE(GICD_IPRIORITYR + reg * 4, val);
}
```

**Register layout**: Each 32-bit register contains 4 priorities (8 bits each).

## timer.c - ARM Generic Timer

### System Register Access

```c
static inline uint32_t read_cntfrq(void)
{
    uint32_t val;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

static inline uint64_t read_cntpct(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(val));
    return val;
}

static inline void write_cntp_tval(uint32_t val)
{
    __asm__ volatile("msr cntp_tval_el0, %0" : : "r"(val));
    __asm__ volatile("isb");
}
```

### Initialization

```c
void timer_init(void)
{
    /* Get timer frequency */
    timer.frequency = read_cntfrq();  /* e.g., 62500000 Hz */

    /* Calculate tick interval */
    timer.tick_interval = timer.frequency / TIMER_FREQ_HZ;
    /* e.g., 62500000 / 100 = 625000 */

    /* Disable timer */
    write_cntp_ctl(0);

    /* Set initial value */
    write_cntp_tval(timer.tick_interval);

    /* Register handler for IRQ 30 */
    irq_register_handler(30, timer_irq_handler);
    gic_enable_irq(30);
}
```

### Timer Interrupt Handler

```c
static void timer_irq_handler(void)
{
    timer.ticks++;

    /* Set next interrupt */
    write_cntp_tval(timer.tick_interval);
}
```

**CNTP_TVAL_EL0**: Countdown timer. When it reaches 0, interrupt fires. Writing a value starts a new countdown.

### Busy-Wait Delay

```c
void timer_delay_ms(uint32_t ms)
{
    uint64_t ticks_per_ms = timer.frequency / 1000;
    uint64_t start = read_cntpct();
    uint64_t target = start + (ticks_per_ms * ms);

    while (read_cntpct() < target) {
        /* Busy wait */
    }
}
```

**Works without interrupts**: Uses the free-running counter (CNTPCT_EL0), not the timer interrupt.

## Debugging

### Check Vector Table Installation

```c
uint64_t vbar;
__asm__ volatile("mrs %0, vbar_el1" : "=r"(vbar));
kprintf("VBAR_EL1: %p\n", (void *)vbar);
```

Should match address of `exception_vector_table`.

### Check Interrupt Masking

```c
uint64_t daif;
__asm__ volatile("mrs %0, daif" : "=r"(daif));
kprintf("IRQ masked: %u\n", (daif >> 7) & 1);
```

0 = enabled, 1 = masked.

### Dump Exception Counters

```c
extern uint64_t exception_counters[16];
for (int i = 0; i < 16; i++) {
    if (exception_counters[i] > 0) {
        kprintf("Vector %d: %llu\n", i, exception_counters[i]);
    }
}
```

### Trigger Test Exception

```c
/* Data abort */
*(volatile uint32_t *)0xBAADF00D = 0;

/* SVC */
__asm__ volatile("svc #0");
```