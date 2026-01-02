/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/interrupts.h
 * Description: Interrupt subsystem interface
 * ============================================================================ */

#ifndef AEOS_INTERRUPTS_H
#define AEOS_INTERRUPTS_H

#include <aeos/types.h>

/* Exception types */
typedef enum {
    EXC_SYNC = 0,       /* Synchronous exception */
    EXC_IRQ  = 1,       /* IRQ interrupt */
    EXC_FIQ  = 2,       /* FIQ interrupt */
    EXC_SERR = 3        /* SError (async abort) */
} exception_type_t;

/* Exception source (which EL it came from) */
typedef enum {
    EXC_FROM_CURRENT_SP0 = 0,   /* Current EL with SP0 */
    EXC_FROM_CURRENT_SPX = 1,   /* Current EL with SPx */
    EXC_FROM_LOWER_A64   = 2,   /* Lower EL using AArch64 */
    EXC_FROM_LOWER_A32   = 3    /* Lower EL using AArch32 */
} exception_source_t;

/**
 * CPU context saved during exception
 * This structure is defined in vectors.asm
 */
typedef struct {
    uint64_t x[31];     /* General purpose registers x0-x30 */
    uint64_t sp;        /* Stack pointer */
    uint64_t pc;        /* Program counter (ELR_EL1) */
    uint64_t pstate;    /* Program state (SPSR_EL1) */
} cpu_context_t;

/**
 * Interrupt handler function type
 */
typedef void (*irq_handler_t)(void);

/**
 * Initialize interrupt subsystem
 * - Installs exception vector table
 * - Initializes GIC
 * - Enables interrupts
 */
void interrupts_init(void);

/**
 * Enable interrupts (unmask IRQ)
 */
void interrupts_enable(void);

/**
 * Disable interrupts (mask IRQ)
 */
void interrupts_disable(void);

/**
 * Register an IRQ handler
 *
 * @param irq IRQ number
 * @param handler Handler function
 */
void irq_register_handler(uint32_t irq, irq_handler_t handler);

/**
 * Unregister an IRQ handler
 *
 * @param irq IRQ number
 */
void irq_unregister_handler(uint32_t irq);

/**
 * Get exception syndrome information
 */
uint64_t get_exception_syndrome(void);

/**
 * Get faulting address (for data/instruction aborts)
 */
uint64_t get_fault_address(void);

/**
 * Dump critical system registers for debugging
 * Shows: VBAR, ELR, SPSR, ESR, FAR, stack pointers, DAIF, MMU state
 */
void dump_system_registers(void);

/* Assembly functions from vectors.asm */
extern void exception_vector_table(void);

#endif /* AEOS_INTERRUPTS_H */
