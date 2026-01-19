/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/interrupts/vectors.asm
 * Description: Exception vector table and handlers
 * ============================================================================ */

include(`src/boot/macros.m4')

/* Stack frame size for saved context */
.set CONTEXT_SIZE, 272   /* 34 registers * 8 bytes */

/* ============================================================================
 * Macros for saving and restoring CPU context
 * ============================================================================ */

.macro SAVE_CONTEXT
    /* Make space on stack for context */
    sub sp, sp, #CONTEXT_SIZE

    /* Save general purpose registers */
    stp x0, x1, [sp, #(16 * 0)]
    stp x2, x3, [sp, #(16 * 1)]
    stp x4, x5, [sp, #(16 * 2)]
    stp x6, x7, [sp, #(16 * 3)]
    stp x8, x9, [sp, #(16 * 4)]
    stp x10, x11, [sp, #(16 * 5)]
    stp x12, x13, [sp, #(16 * 6)]
    stp x14, x15, [sp, #(16 * 7)]
    stp x16, x17, [sp, #(16 * 8)]
    stp x18, x19, [sp, #(16 * 9)]
    stp x20, x21, [sp, #(16 * 10)]
    stp x22, x23, [sp, #(16 * 11)]
    stp x24, x25, [sp, #(16 * 12)]
    stp x26, x27, [sp, #(16 * 13)]
    stp x28, x29, [sp, #(16 * 14)]

    /* Save x30 (LR) and calculate pre-exception SP */
    str x30, [sp, #240]

    /* Save SP value (before we subtracted CONTEXT_SIZE) */
    add x0, sp, #CONTEXT_SIZE
    str x0, [sp, #248]

    /* Save PC (ELR_EL1) and PSTATE (SPSR_EL1) */
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #256]
.endm

.macro RESTORE_CONTEXT
    /* Restore PC and PSTATE */
    ldp x0, x1, [sp, #256]
    msr elr_el1, x0
    msr spsr_el1, x1

    /* Restore x30 (no need to restore SP, we'll adjust it below) */
    ldr x30, [sp, #240]

    /* Restore general purpose registers */
    ldp x28, x29, [sp, #(16 * 14)]
    ldp x26, x27, [sp, #(16 * 13)]
    ldp x24, x25, [sp, #(16 * 12)]
    ldp x22, x23, [sp, #(16 * 11)]
    ldp x20, x21, [sp, #(16 * 10)]
    ldp x18, x19, [sp, #(16 * 9)]
    ldp x16, x17, [sp, #(16 * 8)]
    ldp x14, x15, [sp, #(16 * 7)]
    ldp x12, x13, [sp, #(16 * 6)]
    ldp x10, x11, [sp, #(16 * 5)]
    ldp x8, x9, [sp, #(16 * 4)]
    ldp x6, x7, [sp, #(16 * 3)]
    ldp x4, x5, [sp, #(16 * 2)]
    ldp x2, x3, [sp, #(16 * 1)]
    ldp x0, x1, [sp, #(16 * 0)]

    /* Restore stack pointer */
    add sp, sp, #CONTEXT_SIZE
.endm

/* ============================================================================
 * Exception vector table
 * Must be aligned to 2KB (0x800)
 * Each entry is 128 bytes (0x80)
 * Total size: 16 entries * 128 bytes = 2KB
 * ============================================================================ */

    .section .text.vectors
    .balign 2048
    .global exception_vector_table
exception_vector_table:

/* ----------------------------------------------------------------------------
 * Current EL with SP0
 * ---------------------------------------------------------------------------- */
    .balign 128
el1_sp0_sync:
    SAVE_CONTEXT

    /* INCREMENT COUNTER: sp0_sync_count */
    stp x0, x1, [sp, #-16]!
    adr x0, exception_counters
    ldr x1, [x0, #0]        /* Load sp0_sync_count */
    add x1, x1, #1          /* Increment */
    str x1, [x0, #0]        /* Store back */
    ldp x0, x1, [sp], #16

    /* DEBUG: Print that we entered SP0 SYNC handler */
    stp x0, x1, [sp, #-16]!
    adr x0, sp0_sync_msg
    bl kprintf
    ldp x0, x1, [sp], #16

    /* Check if this is an SVC instruction */
    mrs x0, esr_el1
    lsr x1, x0, #26
    cmp x1, #0x15           /* EC = 0x15 means SVC */
    bne 1f

    /* This is an SVC - extract arguments and call syscall_handler */
    ldr x0, [sp, #(16 * 4)]     /* x8 = syscall number */
    ldr x1, [sp, #(16 * 0)]     /* x0 = arg0 */
    ldr x2, [sp, #(16 * 0 + 8)] /* x1 = arg1 */
    ldr x3, [sp, #(16 * 1)]     /* x2 = arg2 */
    ldr x4, [sp, #(16 * 1 + 8)] /* x3 = arg3 */
    ldr x5, [sp, #(16 * 2)]     /* x4 = arg4 */
    ldr x6, [sp, #(16 * 2 + 8)] /* x5 = arg5 */

    bl syscall_handler

    str x0, [sp, #(16 * 0)]     /* Store return value */

    RESTORE_CONTEXT
    eret

1:  /* Not SVC - handle as generic exception */
    mov x0, #0          /* exception_source = 0 */
    mov x1, #0          /* exception_type = SYNC */
    mov x2, sp          /* context pointer */
    bl handle_exception
    RESTORE_CONTEXT
    eret

    .balign 128
el1_sp0_irq:
    SAVE_CONTEXT

    /* INCREMENT COUNTER: sp0_irq_count */
    stp x0, x1, [sp, #-16]!
    adr x0, exception_counters
    ldr x1, [x0, #8]        /* Load counter[1] = sp0_irq */
    add x1, x1, #1          /* Increment */
    str x1, [x0, #8]        /* Store back */
    ldp x0, x1, [sp], #16

    mov x0, #0
    mov x1, #1          /* exception_type = IRQ */
    mov x2, sp
    bl handle_irq
    RESTORE_CONTEXT
    eret

    .balign 128
el1_sp0_fiq:
    SAVE_CONTEXT

    /* INCREMENT COUNTER: sp0_fiq_count */
    stp x0, x1, [sp, #-16]!
    adr x0, exception_counters
    ldr x1, [x0, #16]       /* Load counter[2] = sp0_fiq */
    add x1, x1, #1          /* Increment */
    str x1, [x0, #16]       /* Store back */
    ldp x0, x1, [sp], #16

    /* Handle FIQ (timer interrupt on QEMU virt) */
    mov x0, #0
    mov x1, #2          /* exception_type = FIQ */
    mov x2, sp
    bl handle_fiq
    RESTORE_CONTEXT
    eret

    .balign 128
el1_sp0_serror:
    SAVE_CONTEXT
    mov x0, #0
    mov x1, #3          /* exception_type = SERR */
    mov x2, sp
    bl handle_exception
    RESTORE_CONTEXT
    eret

/* ----------------------------------------------------------------------------
 * Current EL with SPx
 * ---------------------------------------------------------------------------- */
    .balign 128
el1_spx_sync:
    SAVE_CONTEXT

    /* INCREMENT COUNTER: spx_sync_count */
    stp x0, x1, [sp, #-16]!
    adr x0, exception_counters
    ldr x1, [x0, #32]       /* Load counter[4] = spx_sync */
    add x1, x1, #1          /* Increment */
    str x1, [x0, #32]       /* Store back */
    ldp x0, x1, [sp], #16

    /* Check if this is an SVC instruction by examining ESR_EL1 */
    mrs x0, esr_el1
    lsr x1, x0, #26         /* Extract EC (Exception Class) bits [31:26] */
    cmp x1, #0x15           /* EC = 0x15 means SVC instruction from AArch64 */
    bne 1f                  /* Not SVC, handle as generic exception */

    /* This is an SVC - extract arguments from saved context */
    /* Syscall number in x8, args in x0-x5 */
    ldr x0, [sp, #(16 * 4)]     /* x8 = syscall number */
    ldr x1, [sp, #(16 * 0)]     /* x0 = arg0 */
    ldr x2, [sp, #(16 * 0 + 8)] /* x1 = arg1 */
    ldr x3, [sp, #(16 * 1)]     /* x2 = arg2 */
    ldr x4, [sp, #(16 * 1 + 8)] /* x3 = arg3 */
    ldr x5, [sp, #(16 * 2)]     /* x4 = arg4 */
    ldr x6, [sp, #(16 * 2 + 8)] /* x5 = arg5 */

    /* Call syscall handler */
    bl syscall_handler

    /* Store return value back to saved x0 */
    str x0, [sp, #(16 * 0)]

    RESTORE_CONTEXT
    eret

1:  /* Not an SVC - handle as generic exception */
    mov x0, #1          /* exception_source = 1 */
    mov x1, #0          /* exception_type = SYNC */
    mov x2, sp
    bl handle_exception
    RESTORE_CONTEXT
    eret

    .balign 128
el1_spx_irq:
    SAVE_CONTEXT

    /* INCREMENT COUNTER: spx_irq_count */
    stp x0, x1, [sp, #-16]!
    adr x0, exception_counters
    ldr x1, [x0, #40]       /* Load counter[5] = spx_irq */
    add x1, x1, #1          /* Increment */
    str x1, [x0, #40]       /* Store back */
    ldp x0, x1, [sp], #16

    mov x0, #1
    mov x1, #1          /* exception_type = IRQ */
    mov x2, sp
    bl handle_irq
    RESTORE_CONTEXT
    eret

    .balign 128
el1_spx_fiq:
    SAVE_CONTEXT

    /* INCREMENT COUNTER: spx_fiq_count */
    stp x0, x1, [sp, #-16]!
    adr x0, exception_counters
    ldr x1, [x0, #48]       /* Load counter[6] = spx_fiq */
    add x1, x1, #1          /* Increment */
    str x1, [x0, #48]       /* Store back */
    ldp x0, x1, [sp], #16

    /* Handle FIQ (timer interrupt on QEMU virt) */
    mov x0, #1
    mov x1, #2          /* exception_type = FIQ */
    mov x2, sp
    bl handle_fiq
    RESTORE_CONTEXT
    eret

    .balign 128
el1_spx_serror:
    SAVE_CONTEXT
    mov x0, #1
    mov x1, #3          /* exception_type = SERR */
    mov x2, sp
    bl handle_exception
    RESTORE_CONTEXT
    eret

/* ----------------------------------------------------------------------------
 * Lower EL using AArch64
 * ---------------------------------------------------------------------------- */
    .balign 128
el0_aarch64_sync:
    SAVE_CONTEXT
    mov x0, #2          /* exception_source = 2 */
    mov x1, #0          /* exception_type = SYNC */
    mov x2, sp
    bl handle_exception
    RESTORE_CONTEXT
    eret

    .balign 128
el0_aarch64_irq:
    SAVE_CONTEXT
    mov x0, #2
    mov x1, #1          /* exception_type = IRQ */
    mov x2, sp
    bl handle_exception
    RESTORE_CONTEXT
    eret

    .balign 128
el0_aarch64_fiq:
    SAVE_CONTEXT
    mov x0, #2
    mov x1, #2          /* exception_type = FIQ */
    mov x2, sp
    bl handle_exception
    RESTORE_CONTEXT
    eret

    .balign 128
el0_aarch64_serror:
    SAVE_CONTEXT
    mov x0, #2
    mov x1, #3          /* exception_type = SERR */
    mov x2, sp
    bl handle_exception
    RESTORE_CONTEXT
    eret

/* ----------------------------------------------------------------------------
 * Lower EL using AArch32
 * ---------------------------------------------------------------------------- */
    .balign 128
el0_aarch32_sync:
    SAVE_CONTEXT
    mov x0, #3          /* exception_source = 3 */
    mov x1, #0          /* exception_type = SYNC */
    mov x2, sp
    bl handle_exception
    RESTORE_CONTEXT
    eret

    .balign 128
el0_aarch32_irq:
    SAVE_CONTEXT
    mov x0, #3
    mov x1, #1          /* exception_type = IRQ */
    mov x2, sp
    bl handle_exception
    RESTORE_CONTEXT
    eret

    .balign 128
el0_aarch32_fiq:
    SAVE_CONTEXT
    mov x0, #3
    mov x1, #2          /* exception_type = FIQ */
    mov x2, sp
    bl handle_exception
    RESTORE_CONTEXT
    eret

    .balign 128
el0_aarch32_serror:
    SAVE_CONTEXT
    mov x0, #3
    mov x1, #3          /* exception_type = SERR */
    mov x2, sp
    bl handle_exception
    RESTORE_CONTEXT
    eret

/* ============================================================================
 * Helper functions
 * ============================================================================ */

/**
 * Enable IRQ interrupts
 */
    .global interrupts_enable
    .balign 4
interrupts_enable:
    msr daifclr, #3     /* Clear FIQ and IRQ mask bits (bits 1 and 2) */
    ISB
    ret

/**
 * Disable IRQ interrupts
 */
    .global interrupts_disable
    .balign 4
interrupts_disable:
    msr daifset, #3     /* Set FIQ and IRQ mask bits (bits 1 and 2) */
    ISB
    ret

/* ============================================================================
 * Debug messages
 * ============================================================================ */
    .section .data
    .global exception_counters
exception_counters:
    .quad 0  /*  0: el1_sp0_sync */
    .quad 0  /*  1: el1_sp0_irq */
    .quad 0  /*  2: el1_sp0_fiq */
    .quad 0  /*  3: el1_sp0_serror */
    .quad 0  /*  4: el1_spx_sync */
    .quad 0  /*  5: el1_spx_irq */
    .quad 0  /*  6: el1_spx_fiq */
    .quad 0  /*  7: el1_spx_serror */
    .quad 0  /*  8: el0_aarch64_sync */
    .quad 0  /*  9: el0_aarch64_irq */
    .quad 0  /* 10: el0_aarch64_fiq */
    .quad 0  /* 11: el0_aarch64_serror */
    .quad 0  /* 12: el0_aarch32_sync */
    .quad 0  /* 13: el0_aarch32_irq */
    .quad 0  /* 14: el0_aarch32_fiq */
    .quad 0  /* 15: el0_aarch32_serror */

    .section .rodata
sp0_sync_msg:
    .asciz "[ASM DEBUG] el1_sp0_sync entered\n"
fiq_entry_msg:
    .asciz "[ASM DEBUG] el1_sp0_fiq entered\n"
svc_detected_msg:
    .asciz "[ASM DEBUG] SVC detected in FIQ handler\n"

/* ============================================================================
 * End of vectors.asm
 * ============================================================================ */
