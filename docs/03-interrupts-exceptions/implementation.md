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

Each of the 16 vector slots is exactly 128 bytes (0x80) and holds a single branch to its handler body. The bodies live in a block below the 2KB table:

```assembly
    .balign 128                     /* Start of slot (one per 0x80) */
    b el1_spx_irq                   /* VBAR + 0x280 */
    /* ... 15 more slots ... */

    .balign 2048                    /* Close the table at exactly base+0x800 */
el1_spx_irq:                        /* Handler body, reached by the stub above */
    SAVE_CONTEXT
    /* ... handle IRQ ... */
    RESTORE_CONTEXT
    eret
```

Keeping each slot to a single branch is what guarantees the architectural offsets (`VBAR + N*0x80`). An inlined body that grows past 0x80 shifts every later slot and silently misaligns the table -- the bug that once routed the EL1h IRQ slot into the FIQ handler body and masked the timer-as-IRQ servicing.

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

### Synchronous Exceptions and the SVC Class

The synchronous vector decodes the exception class from ESR_EL1 to triage faults (data abort, instruction abort, SP alignment, SVC). The kernel's own EL1 syscalls do not trap through this vector: `syscall(num, args...)` is a direct C function-call table lookup, since there is no EL1->EL1 privilege boundary to cross. The EL0 entry is different - see "SVC from EL0" below.

```assembly
el1_spx_sync:
    SAVE_CONTEXT

    /* Decode ESR_EL1 exception class for diagnostics */
    mrs x0, esr_el1
    lsr x1, x0, #26             /* EC = bits [31:26] */

    /* Dispatch to the generic C handler with source/type/context */
    bl handle_exception

    RESTORE_CONTEXT
    eret
```

### SVC from EL0

`el0_aarch64_sync` (VBAR+0x400, lower-EL AArch64 synchronous) carries the svc-decode block, a verbatim copy of the proven `el1_spx_sync` decode: check ESR_EL1 EC=0x15, pull the syscall number (x8) and arguments (x0-x5) from the saved register frame, `bl syscall_handler`, store the return value back into the frame's x0 slot, then `RESTORE_CONTEXT; eret`. The `eret` returns to EL0 because the saved SPSR is EL0t, not because of anything in the handler. The non-SVC branch routes through `handle_el0_sync` (in `src/proc/usermode.c`): a trapped privileged instruction from EL0 (such as `msr daifset`, EC=0x18) is recorded for the test seam when armed, and otherwise reported through the halting `handle_exception`. This is the Phase 5 EL0/EL1 boundary; see Section 05.

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

We use `#3` to clear both the FIQ and IRQ mask bits. The timer arrives as an IRQ (Group 0, `FIQEn = 0`); unmasking FIQ as well is harmless and keeps the dormant FIQ fallback path live.

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
    /* Mask debug, SError, IRQ, and FIQ before touching kprintf. The print path
     * is not reentrant, so a timer FIQ landing here would corrupt the trace. */
    __asm__ volatile("msr DAIFSet, #0xF" ::: "memory");

    /* If a stack overflow brought us here, the boot-stack sentinel is clobbered.
     * Check it before the first kprintf so the panic names the offending PC. */
    stack_guard_check(context->pc);

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

**DAIF mask**: `msr DAIFSet, #0xF` runs first so a timer FIQ cannot recurse into the non-reentrant `kprintf` while the crash dump is printing. This is the BUG-15 fix; the mask is scoped to the handler entry, not to normal operation (the timer runs unmasked otherwise). The DAIF mask is the same-core exclusion; the cross-core exclusion (so two cores in `putchar` cannot splice a torn ring index) is `kprintf_ring_lock` in kprintf.c. The panic reader (`kprintf_ring_walk`) takes that lock with `spin_trylock`-or-bypass and reads the ring whether or not it acquires, so a wedged core holding the lock can never deadlock the dump.

**stack_guard_check**: Called immediately after the mask, before the first `kprintf`. If a kernel stack overflow clobbered the sentinel at `__stack_limit`, this reports a deterministic `klog_fatal` naming `context->pc` (the saved `ELR_EL1`) and halts, rather than letting the backtrace walk a corrupt stack. The same check runs on every timer tick from `timer_handle_fiq` with a PC argument of 0. See `src/kernel/stack_guard.c`.

**ESR_EL1**: Exception Syndrome Register - describes what went wrong
**FAR_EL1**: Fault Address Register - virtual address that caused fault

### IRQ Handler

```c
void handle_irq(uint32_t source, uint32_t type, cpu_context_t *context)
{
    uint32_t irq = gic_acknowledge_irq();

    /* GICv2 reserves IDs 1020-1023 as spurious indicators. They must not be
     * EOI'd and must never be looked up in the handler table. */
    if (irq >= 1020) {
        return;
    }

    if (irq_handlers[irq] != NULL) {
        irq_handlers[irq]();
    }

    gic_end_of_irq(irq);
}
```

**Flow**:
1. Read GICC_IAR to get IRQ number and acknowledge
2. Filter the GICv2 spurious range (1020-1023) and return without an EOI
3. Call any registered handler
4. Write to GICC_EOIR to signal completion

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
    if (irq < 1020) {
        if (irq_handlers[irq]) {
            irq_handlers[irq]();
        }
        gic_end_of_irq(irq);
    }
    /* IDs 1020-1023 are GICv2 spurious indicators; drop them silently. */
}
```

**Status of this path:** `handle_fiq` is a dormant fallback. The timer is delivered as an IRQ (Group 0, `FIQEn = 0`) and serviced by `handle_irq` -> `timer_irq_handler`, which goes through the normal GIC acknowledge flow. The direct ISTATUS check here (via `timer_handle_fiq`) is retained only in case a future source is routed as FIQ. Earlier revisions believed the timer arrived as FIQ "regardless of GIC group configuration" -- that was an artifact of a misaligned vector table, not a property of QEMU. See the README's "Timer Interrupt Delivery" section.

**Why 1020 and not GIC_MAX_IRQ?** GICv2 reserves IDs 1020-1023 as spurious-interrupt indicators (1022 = group-0 spurious, 1023 = group-1 spurious). Using `< 1020` correctly drops them; an earlier version compared against the full table size and produced thousands of "Unhandled IRQ: 1022" log spam on every redraw tick.

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

The kernel uses the **virtual** timer (CNTV), the right choice for non-secure EL1 on QEMU virt. The PPI for the virtual timer is **IRQ 27**.

### System Register Access

```c
static inline uint32_t read_cntfrq(void)
{
    uint32_t val;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

static inline uint64_t read_cntvct(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
}

static inline void write_cntv_tval(int32_t val)
{
    __asm__ volatile("msr cntv_tval_el0, %0" : : "r"(val));
    __asm__ volatile("isb");
}

static inline uint32_t read_cntv_ctl(void)
{
    uint32_t val;
    __asm__ volatile("mrs %0, cntv_ctl_el0" : "=r"(val));
    return val;
}
```

### Initialization

```c
void timer_init(void)
{
    /* Get timer frequency */
    timer.frequency = read_cntfrq();  /* e.g., 62500000 Hz on QEMU virt */

    /* Calculate tick interval */
    timer.tick_interval = timer.frequency / TIMER_FREQ_HZ;

    write_cntv_ctl(0);                          /* disable while configuring */
    write_cntv_tval(timer.tick_interval);

    irq_register_handler(27, timer_irq_handler); /* virtual timer PPI */
    gic_set_priority(27, GIC_PRIORITY_HIGH);
    gic_enable_irq(27);
}
```

`timer_start` is a separate call invoked after `interrupts_enable()` so the kernel doesn't take spurious interrupts during init.

### Timer Interrupt Handler

```c
static void timer_irq_handler(void)
{
    stack_guard_check(0);                  /* live tick path: cheap overflow check */
    timer.ticks++;
    write_cntv_tval(timer.tick_interval);  /* re-arm, clears the condition */
    scheduler_tick();                      /* drive preemption */
}
```

This is the live tick path: the timer arrives as an IRQ and `handle_irq` dispatches `timer_irq_handler`. The dormant FIQ fallback (`timer_handle_fiq`, which checks `CNTV_CTL`'s ISTATUS and only acts when the bit is set; see the FIQ Handler section above) reaches the same re-arm + `scheduler_tick` work.

### Wall-clock Uptime

```c
uint64_t timer_get_uptime_ms(void)
{
    return (read_cntvct() * 1000) / timer.frequency;
}
```

This deliberately reads `CNTVCT_EL0` directly instead of returning `timer.ticks * 10`. The interrupt-driven counter falls behind real time during busy-wait loops (bootscreen fades, window animations), so animations would appear frozen. CNTVCT keeps advancing regardless.

### Busy-Wait Delay

```c
void timer_delay_ms(uint32_t ms)
{
    uint64_t ticks_per_ms = timer.frequency / 1000;
    uint64_t start = read_cntvct();
    uint64_t target = start + (ticks_per_ms * ms);

    while (read_cntvct() < target) {
        /* Busy wait */
    }
}
```

**Works without interrupts**: uses the free-running virtual counter, not the timer interrupt.

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