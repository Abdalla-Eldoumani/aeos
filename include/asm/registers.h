/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/asm/registers.h
 * Description: ARMv8 system register definitions
 * ============================================================================ */

#ifndef ASM_REGISTERS_H
#define ASM_REGISTERS_H

/* ============================================================================
 * Exception Level Registers
 * ============================================================================ */

/* Current Exception Level */
#define CURRENTEL_EL_MASK   0xC
#define CURRENTEL_EL_SHIFT  2

/* ============================================================================
 * System Control Registers
 * ============================================================================ */

/* SCTLR_EL1 - System Control Register */
#define SCTLR_M_BIT         (1 << 0)    /* MMU enable */
#define SCTLR_A_BIT         (1 << 1)    /* Alignment check enable */
#define SCTLR_C_BIT         (1 << 2)    /* Data cache enable */
#define SCTLR_SA_BIT        (1 << 3)    /* Stack alignment check */
#define SCTLR_I_BIT         (1 << 12)   /* Instruction cache enable */

/* ============================================================================
 * Interrupt Control Registers
 * ============================================================================ */

/* DAIF - Interrupt mask bits */
#define DAIF_DBG_BIT        (1 << 9)    /* Debug exceptions */
#define DAIF_ABT_BIT        (1 << 8)    /* SError (async abort) */
#define DAIF_IRQ_BIT        (1 << 7)    /* IRQ */
#define DAIF_FIQ_BIT        (1 << 6)    /* FIQ */

/* SPSR_EL1 - Saved Program Status Register */
#define SPSR_MODE_MASK      0x1F
#define SPSR_MODE_EL0t      0x00
#define SPSR_MODE_EL1t      0x04
#define SPSR_MODE_EL1h      0x05
#define SPSR_MODE_EL2t      0x08
#define SPSR_MODE_EL2h      0x09

#define SPSR_F_BIT          (1 << 6)    /* FIQ mask */
#define SPSR_I_BIT          (1 << 7)    /* IRQ mask */
#define SPSR_A_BIT          (1 << 8)    /* SError mask */
#define SPSR_D_BIT          (1 << 9)    /* Debug mask */

/* ============================================================================
 * Exception Syndrome Register (ESR_EL1)
 * ============================================================================ */

#define ESR_EC_SHIFT        26
#define ESR_EC_MASK         0x3F
#define ESR_IL_BIT          (1 << 25)
#define ESR_ISS_MASK        0x1FFFFFF

/* Exception Class codes */
#define ESR_EC_UNKNOWN      0x00
#define ESR_EC_WFI_WFE      0x01
#define ESR_EC_SVC_AARCH64  0x15
#define ESR_EC_INST_ABORT   0x20
#define ESR_EC_INST_ABORT_L 0x21
#define ESR_EC_PC_ALIGN     0x22
#define ESR_EC_DATA_ABORT   0x24
#define ESR_EC_DATA_ABORT_L 0x25
#define ESR_EC_SP_ALIGN     0x26
#define ESR_EC_BRK          0x3C

/* ============================================================================
 * Generic Timer Registers (CNTP_* - Physical Timer)
 * ============================================================================ */

/* CNTP_CTL_EL0 - Counter-Timer Physical Timer Control */
#define CNTP_CTL_ENABLE     (1 << 0)    /* Timer enable */
#define CNTP_CTL_IMASK      (1 << 1)    /* Timer interrupt mask */
#define CNTP_CTL_ISTATUS    (1 << 2)    /* Timer condition met */

/* Timer interrupt number on QEMU virt */
#define TIMER_IRQ_PHYS      30          /* Physical timer IRQ */

/* ============================================================================
 * GIC System Register Interface (ICC_* registers for GICv3)
 * ============================================================================ */

/* ICC_PMR_EL1 - Interrupt Priority Mask Register */
#define ICC_PMR_PRIORITY    0xFF        /* Lowest priority (allow all) */

/* ICC_IGRPEN1_EL1 - Interrupt Group 1 Enable */
#define ICC_IGRPEN1_EN      (1 << 0)    /* Enable Group 1 interrupts */

/* ICC_BPR1_EL1 - Binary Point Register Group 1 */
#define ICC_BPR1_BINARY_POINT  0        /* No priority grouping */

/* ============================================================================
 * Vector Base Address Register (VBAR_EL1)
 * ============================================================================ */

#define VBAR_ALIGN          0x800       /* 2KB alignment required */

#endif /* ASM_REGISTERS_H */
