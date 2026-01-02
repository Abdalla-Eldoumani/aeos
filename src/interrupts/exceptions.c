/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/interrupts/exceptions.c
 * Description: Exception and interrupt handlers
 * ============================================================================ */

#include <aeos/interrupts.h>
#include <aeos/gic.h>
#include <aeos/kprintf.h>
#include <aeos/types.h>

/* IRQ handler table */
static irq_handler_t irq_handlers[GIC_MAX_IRQ];

/* Exception statistics */
static struct {
    uint64_t sync_count;
    uint64_t irq_count;
    uint64_t fiq_count;
    uint64_t serr_count;
} exception_stats;

/**
 * Get Exception Syndrome Register (ESR_EL1)
 */
uint64_t get_exception_syndrome(void)
{
    uint64_t esr;
    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
    return esr;
}

/**
 * Get Fault Address Register (FAR_EL1)
 */
uint64_t get_fault_address(void)
{
    uint64_t far;
    __asm__ volatile("mrs %0, far_el1" : "=r"(far));
    return far;
}

/**
 * Get exception class from ESR
 */
static uint32_t get_exception_class(uint64_t esr)
{
    return (esr >> 26) & 0x3F;
}

/**
 * Print exception class name
 */
static void print_exception_class(uint32_t ec)
{
    switch (ec) {
        case 0x00: kprintf("Unknown"); break;
        case 0x01: kprintf("WFI/WFE"); break;
        case 0x15: kprintf("SVC (AArch64)"); break;
        case 0x20: kprintf("Instruction Abort (lower EL)"); break;
        case 0x21: kprintf("Instruction Abort (same EL)"); break;
        case 0x22: kprintf("PC Alignment"); break;
        case 0x24: kprintf("Data Abort (lower EL)"); break;
        case 0x25: kprintf("Data Abort (same EL)"); break;
        case 0x26: kprintf("SP Alignment"); break;
        case 0x3C: kprintf("BRK instruction"); break;
        default: kprintf("EC=0x%x", ec); break;
    }
}

/**
 * Generic exception handler
 * Called from assembly exception vectors
 *
 * @param source Exception source (which EL)
 * @param type Exception type (sync/irq/fiq/serr)
 * @param context Saved CPU context
 */
void handle_exception(uint32_t source, uint32_t type, cpu_context_t *context)
{
    uint64_t esr, far, ec;

    /* Update statistics */
    switch (type) {
        case EXC_SYNC: exception_stats.sync_count++; break;
        case EXC_IRQ:  exception_stats.irq_count++; break;
        case EXC_FIQ:  exception_stats.fiq_count++; break;
        case EXC_SERR: exception_stats.serr_count++; break;
    }

    /* Get exception information */
    esr = get_exception_syndrome();
    far = get_fault_address();
    ec = get_exception_class(esr);

    /* Print exception details */
    kprintf("\n");
    kprintf("======== EXCEPTION ========\n");

    /* Exception type */
    kprintf("Type: ");
    switch (type) {
        case EXC_SYNC: kprintf("Synchronous\n"); break;
        case EXC_IRQ:  kprintf("IRQ\n"); break;
        case EXC_FIQ:  kprintf("FIQ\n"); break;
        case EXC_SERR: kprintf("SError\n"); break;
    }

    /* Exception source */
    kprintf("Source: ");
    switch (source) {
        case EXC_FROM_CURRENT_SP0: kprintf("Current EL (SP0)\n"); break;
        case EXC_FROM_CURRENT_SPX: kprintf("Current EL (SPx)\n"); break;
        case EXC_FROM_LOWER_A64:   kprintf("Lower EL (AArch64)\n"); break;
        case EXC_FROM_LOWER_A32:   kprintf("Lower EL (AArch32)\n"); break;
    }

    /* Exception class */
    kprintf("Class: ");
    print_exception_class(ec);
    kprintf("\n");

    /* Registers */
    kprintf("PC:     %p\n", (void *)context->pc);
    kprintf("SP:     %p\n", (void *)context->sp);
    kprintf("ESR:    %p\n", (void *)esr);
    kprintf("FAR:    %p\n", (void *)far);
    kprintf("PSTATE: %p\n", (void *)context->pstate);

    kprintf("===========================\n");

    /* Halt system on unexpected exceptions */
    klog_fatal("Unhandled exception - system halted");
    while (1) {
        __asm__ volatile("wfi");
    }
}

/**
 * IRQ handler
 * Called from assembly IRQ vector
 */
void handle_irq(uint32_t source, uint32_t type, cpu_context_t *context)
{
    uint32_t irq;
    irq_handler_t handler;

    (void)source;   /* Unused */
    (void)type;     /* Unused */
    (void)context;  /* Unused for now */

    /* Update statistics */
    exception_stats.irq_count++;

    /* Acknowledge interrupt and get IRQ number */
    irq = gic_acknowledge_irq();

    /* Spurious interrupt check */
    if (irq >= GIC_MAX_IRQ) {
        return;
    }

    /* Call registered handler */
    handler = irq_handlers[irq];
    if (handler != NULL) {
        handler();
    } else {
        klog_warn("Unhandled IRQ: %u", irq);
    }

    /* Signal end of interrupt */
    gic_end_of_irq(irq);
}

/**
 * Register an IRQ handler
 */
void irq_register_handler(uint32_t irq, irq_handler_t handler)
{
    if (irq >= GIC_MAX_IRQ) {
        klog_error("Invalid IRQ number: %u", irq);
        return;
    }

    irq_handlers[irq] = handler;
    klog_debug("Registered handler for IRQ %u", irq);
}

/**
 * Unregister an IRQ handler
 */
void irq_unregister_handler(uint32_t irq)
{
    if (irq >= GIC_MAX_IRQ) {
        klog_error("Invalid IRQ number: %u", irq);
        return;
    }

    irq_handlers[irq] = NULL;
    klog_debug("Unregistered handler for IRQ %u", irq);
}

/**
 * Dump critical system registers for debugging
 */
void dump_system_registers(void)
{
    uint64_t vbar, sp, currentel, spsel;
    uint64_t daif;

    kprintf("\n");
    kprintf("========================================\n");
    kprintf("  System Register Dump\n");
    kprintf("========================================\n");

    /* Read basic registers one at a time with debugging */
    kprintf("Reading VBAR_EL1...\n");
    __asm__ volatile("mrs %0, vbar_el1" : "=r"(vbar));
    kprintf("  VBAR_EL1:  0x%016x\n", vbar);

    kprintf("Reading CurrentEL...\n");
    __asm__ volatile("mrs %0, currentel" : "=r"(currentel));
    kprintf("  CurrentEL:  EL%u\n", (uint32_t)((currentel >> 2) & 3));

    kprintf("Reading SPSel...\n");
    __asm__ volatile("mrs %0, spsel" : "=r"(spsel));
    kprintf("  SPSel:      %u\n", (uint32_t)(spsel & 1));

    kprintf("Reading SP...\n");
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    kprintf("  SP:         0x%016x\n", sp);

    kprintf("Reading DAIF...\n");
    __asm__ volatile("mrs %0, daif" : "=r"(daif));
    kprintf("  DAIF:       0x%x\n", (uint32_t)(daif >> 6) & 0xF);

    /* SKIP potentially problematic registers for now */
    kprintf("(Skipping ELR, SPSR, ESR, FAR, MMU registers)\n");

    kprintf("========================================\n");
    kprintf("\n");
    return;

    /* OLD CODE - commented out to find problematic register */
    #if 0
    uint64_t vbar, elr, spsr, sp, sp_el0, sp_el1, currentel, spsel;
    uint64_t daif, esr, far;
    uint64_t sctlr, tcr, ttbr0, ttbr1;

    /* Exception handling registers */
    __asm__ volatile("mrs %0, vbar_el1" : "=r"(vbar));
    __asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
    __asm__ volatile("mrs %0, spsr_el1" : "=r"(spsr));
    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
    __asm__ volatile("mrs %0, far_el1" : "=r"(far));

    /* Stack pointers */
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    __asm__ volatile("mrs %0, sp_el0" : "=r"(sp_el0));
    __asm__ volatile("mrs %0, sp_el1" : "=r"(sp_el1));
    __asm__ volatile("mrs %0, spsel" : "=r"(spsel));

    /* Exception level */
    __asm__ volatile("mrs %0, currentel" : "=r"(currentel));

    /* Interrupt masking */
    __asm__ volatile("mrs %0, daif" : "=r"(daif));

    /* MMU registers */
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    __asm__ volatile("mrs %0, tcr_el1" : "=r"(tcr));
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0));
    __asm__ volatile("mrs %0, ttbr1_el1" : "=r"(ttbr1));

    kprintf("\n");
    kprintf("========================================\n");
    kprintf("  System Register Dump\n");
    kprintf("========================================\n");

    kprintf("Exception Handling:\n");
    kprintf("  VBAR_EL1:  0x%016x\n", vbar);
    kprintf("  ELR_EL1:   0x%016x\n", elr);
    kprintf("  SPSR_EL1:  0x%016x\n", spsr);
    kprintf("  ESR_EL1:   0x%016x\n", esr);
    kprintf("  FAR_EL1:   0x%016x\n", far);

    kprintf("\nStack Pointers:\n");
    kprintf("  Current SP: 0x%016x\n", sp);
    kprintf("  SP_EL0:     0x%016x\n", sp_el0);
    kprintf("  SP_EL1:     0x%016x\n", sp_el1);
    kprintf("  SPSel:      %u (0=SP_EL0, 1=SP_EL1)\n", (uint32_t)(spsel & 1));

    kprintf("\nException Level:\n");
    kprintf("  CurrentEL:  EL%u\n", (uint32_t)((currentel >> 2) & 3));

    kprintf("\nInterrupt Masking (DAIF):\n");
    kprintf("  D (Debug):  %u\n", (uint32_t)((daif >> 9) & 1));
    kprintf("  A (SError): %u\n", (uint32_t)((daif >> 8) & 1));
    kprintf("  I (IRQ):    %u\n", (uint32_t)((daif >> 7) & 1));
    kprintf("  F (FIQ):    %u\n", (uint32_t)((daif >> 6) & 1));

    kprintf("\nMMU Configuration:\n");
    kprintf("  SCTLR_EL1:  0x%016x\n", sctlr);
    kprintf("    M (MMU):    %u\n", (uint32_t)(sctlr & 1));
    kprintf("    C (DCache): %u\n", (uint32_t)((sctlr >> 2) & 1));
    kprintf("    I (ICache): %u\n", (uint32_t)((sctlr >> 12) & 1));
    kprintf("  TCR_EL1:    0x%016x\n", tcr);
    kprintf("  TTBR0_EL1:  0x%016x\n", ttbr0);
    kprintf("  TTBR1_EL1:  0x%016x\n", ttbr1);

    kprintf("========================================\n");
    kprintf("\n");
    #endif
}

/**
 * Initialize interrupt subsystem
 */
void interrupts_init(void)
{
    uint32_t i;
    uint64_t vbar;

    klog_info("Initializing interrupt subsystem...");

    /* Clear IRQ handler table */
    for (i = 0; i < GIC_MAX_IRQ; i++) {
        irq_handlers[i] = NULL;
    }

    /* Clear exception statistics */
    exception_stats.sync_count = 0;
    exception_stats.irq_count = 0;
    exception_stats.fiq_count = 0;
    exception_stats.serr_count = 0;

    /* Install exception vector table */
    vbar = (uint64_t)&exception_vector_table;
    __asm__ volatile("msr vbar_el1, %0" : : "r"(vbar));
    __asm__ volatile("isb");

    kprintf("  Vector table installed at: %p\n", (void *)vbar);

    klog_info("Interrupt subsystem initialized");
}

/* ============================================================================
 * End of exceptions.c
 * ============================================================================ */
