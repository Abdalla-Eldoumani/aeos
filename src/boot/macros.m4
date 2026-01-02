/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/boot/macros.m4
 * Description: M4 macro definitions for readable ARMv8 assembly
 * ============================================================================ */

/* Change m4 quote characters to avoid conflicts with assembly comments */
changequote(`«', `»')

/* ============================================================================
 * Register Aliases
 * ============================================================================ */

/* General purpose register aliases for common uses */
define(«temp_reg», «x19»)
define(«temp_reg2», «x20»)
define(«temp_reg3», «x21»)
define(«result_reg», «x22»)
define(«counter_reg», «x23»)

/* ============================================================================
 * Stack Frame Macros
 * ============================================================================ */

/* Save frame pointer and link register, set up new frame */
define(«PUSH_FRAME», «stp x29, x30, [sp, -16]!
    mov x29, sp»)

/* Restore frame pointer and link register */
define(«POP_FRAME», «ldp x29, x30, [sp], 16»)

/* Save callee-saved registers (x19-x28) */
define(«PUSH_REGS», «stp x19, x20, [sp, -16]!
    stp x21, x22, [sp, -16]!
    stp x23, x24, [sp, -16]!
    stp x25, x26, [sp, -16]!
    stp x27, x28, [sp, -16]!»)

/* Restore callee-saved registers */
define(«POP_REGS», «ldp x27, x28, [sp], 16
    ldp x25, x26, [sp], 16
    ldp x23, x24, [sp], 16
    ldp x21, x22, [sp], 16
    ldp x19, x20, [sp], 16»)

/* ============================================================================
 * Exception Level Macros
 * ============================================================================ */

/* Drop from EL2 to EL1 */
define(«DROP_TO_EL1», «
    /* Set EL1 execution state to AArch64 */
    mov x0, (1 << 31)           /* EL1 uses AArch64 */
    orr x0, x0, (1 << 1)        /* SWIO hardwired on PE */
    msr hcr_el2, x0

    /* Set SPSR for EL1h (use SP_EL1, mask interrupts) */
    mov x0, 0x3c5               /* EL1h, DAIF masked */
    msr spsr_el2, x0

    /* Set return address to target label */
    adr x0, el1_entry
    msr elr_el2, x0

    /* Exception return to EL1 */
    eret
»)

/* ============================================================================
 * MMU and Cache Control Macros
 * ============================================================================ */

/* Disable MMU and caches */
define(«DISABLE_MMU», «
    mrs x0, sctlr_el1
    bic x0, x0, 1               /* Clear M bit (MMU) */
    bic x0, x0, (1 << 2)        /* Clear C bit (data cache) */
    bic x0, x0, (1 << 12)       /* Clear I bit (instruction cache) */
    msr sctlr_el1, x0
    isb
»)

/* ============================================================================
 * Memory Operation Macros
 * ============================================================================ */

/* Zero a memory region: ZERO_MEMORY(start_reg, end_reg, temp_reg) */
define(«ZERO_MEMORY», «
    mov $3, xzr
1:  cmp $1, $2
    b.ge 2f
    str $3, [$1], 8
    b 1b
2:
»)

/* ============================================================================
 * Utility Macros
 * ============================================================================ */

/* No operation for alignment or timing */
define(«NOP», «nop»)

/* Data synchronization barrier */
define(«DSB», «dsb sy»)

/* Instruction synchronization barrier */
define(«ISB», «isb»)

/* Wait for interrupt (low power mode) */
define(«WFI», «wfi»)

/* Wait for event */
define(«WFE», «wfe»)

/* Send event */
define(«SEV», «sev»)

/* ============================================================================
 * UART Register Offsets (PL011)
 * ============================================================================ */
define(«UART_BASE», «0x09000000»)
define(«UART_DR», «0x00»)
define(«UART_FR», «0x18»)
define(«UART_IBRD», «0x24»)
define(«UART_FBRD», «0x28»)
define(«UART_LCRH», «0x2C»)
define(«UART_CR», «0x30»)
define(«UART_IMSC», «0x38»)

/* UART Flag Register bits */
define(«UART_FR_TXFF», «(1 << 5)»)  /* Transmit FIFO full */
define(«UART_FR_RXFE», «(1 << 4)»)  /* Receive FIFO empty */

/* ============================================================================
 * System Register Access Macros
 * ============================================================================ */

/* Read system register: READ_SYSREG(reg_name, dest_reg) */
define(«READ_SYSREG», «mrs $2, $1»)

/* Write system register: WRITE_SYSREG(reg_name, src_reg) */
define(«WRITE_SYSREG», «msr $1, $2»)

/* ============================================================================
 * End of macros.m4
 * ============================================================================ */
