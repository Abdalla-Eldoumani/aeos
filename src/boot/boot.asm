/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/boot/boot.asm
 * Description: Bootstrap code - first code executed by the kernel
 *              Sets up execution environment and jumps to C kernel
 * ============================================================================ */

include(`src/boot/macros.m4')

    .section .text.boot
    .global _start

/* ============================================================================
 * _start - Kernel entry point
 * Called by bootloader/QEMU with:
 *   - CPU in EL2 or EL3 (QEMU virt board starts in EL2)
 *   - MMU disabled
 *   - Caches disabled
 *   - x0 = device tree blob address (not used initially)
 * ============================================================================ */
_start:
    /* First, check which CPU core we are (only core 0 boots) */
    mrs x1, mpidr_el1
    and x1, x1, 0xFF            /* Extract CPU ID */
    cbz x1, primary_cpu         /* If CPU 0, continue boot */

    /* Secondary CPUs: park them in low-power mode */
secondary_cpu_park:
    WFI
    b secondary_cpu_park

primary_cpu:
    /* Save device tree pointer (x0) for later use */
    mov x19, x0

    /* Check current exception level */
    mrs x0, CurrentEL
    and x0, x0, 0xC             /* Extract EL bits [3:2] */
    lsr x0, x0, 2               /* Shift to get EL number */

    cmp x0, 2                   /* Are we in EL2? */
    b.eq drop_to_el1            /* Yes, drop to EL1 */
    cmp x0, 3                   /* Are we in EL3? */
    b.eq drop_from_el3          /* Yes, drop through EL2 to EL1 */

    /* Already in EL1, continue */
    b el1_entry

/* ============================================================================
 * Drop from EL3 to EL2 (if started in EL3)
 * ============================================================================ */
drop_from_el3:
    /* Set EL2 execution state to AArch64 */
    mov x0, (1 << 10)           /* EL2 uses AArch64 */
    msr scr_el3, x0

    /* Set SPSR for EL2h */
    mov x0, 0x3c9               /* EL2h, DAIF masked */
    msr spsr_el3, x0

    /* Set return address to EL2 entry */
    adr x0, drop_to_el1
    msr elr_el3, x0

    /* Exception return to EL2 */
    eret

/* ============================================================================
 * Drop from EL2 to EL1 (standard QEMU virt board path)
 * ============================================================================ */
drop_to_el1:
    /* Set EL1 execution state to AArch64 */
    mov x0, (1 << 31)           /* RW=1: EL1 uses AArch64 */
    orr x0, x0, (1 << 1)        /* SWIO hardwired on PE */
    msr hcr_el2, x0

    /* Clear virtual timer offset (REQUIRED for virtual timer at EL1) */
    mov x0, #0
    msr cntvoff_el2, x0

    /* Enable EL1 access to physical and virtual timers */
    /* CNTHCTL_EL2: EL1PCTEN=1, EL1PCEN=1 for physical timer access */
    mov x0, #3
    msr cnthctl_el2, x0

    /* Don't trap any coprocessor accesses from EL1 */
    mov x0, #0
    msr cptr_el2, x0
    isb

    /* Set SPSR for EL1h (use SP_EL1, mask all interrupts) */
    mov x0, 0x3c5               /* EL1h, DAIF masked */
    msr spsr_el2, x0

    /* Set return address to EL1 entry */
    adr x0, el1_entry
    msr elr_el2, x0

    /* Exception return to EL1 */
    eret

/* ============================================================================
 * EL1 Entry - Now running in EL1 (kernel mode)
 * ============================================================================ */
el1_entry:
    /* CRITICAL FIX: Set BOTH SP_EL0 and SP_EL1 to the SAME value */
    /* This way it doesn't matter if SPSel changes unexpectedly */

    ldr x0, =__stack_top        /* x0 = 0x4001a000 */

    /* Set SP_EL0 by switching to it */
    msr spsel, #0
    ISB
    mov sp, x0                  /* SP_EL0 = 0x4001a000 */

    /* Set SP_EL1 by switching to it */
    msr spsel, #1
    ISB
    mov sp, x0                  /* SP_EL1 = 0x4001a000 */

    /* Both SP_EL0 and SP_EL1 now point to the same stack */
    /* Interrupts will work regardless of SPSel value */

    /* Disable MMU and caches (should already be disabled) */
    mrs x0, sctlr_el1
    bic x0, x0, 1               /* Clear M bit (MMU) */
    bic x0, x0, (1 << 2)        /* Clear C bit (data cache) */
    bic x0, x0, (1 << 12)       /* Clear I bit (instruction cache) */
    msr sctlr_el1, x0
    ISB

    /* Clear BSS section (uninitialized data) */
    ldr x0, =__bss_start
    ldr x1, =__bss_end
    ZERO_MEMORY(x0, x1, x2)

    /* Restore device tree pointer */
    mov x0, x19

    /* Jump to C kernel entry point */
    bl kernel_main

    /* If kernel_main returns (should never happen), halt */
halt:
    WFI
    b halt

/* ============================================================================
 * End of boot.asm
 * ============================================================================ */
