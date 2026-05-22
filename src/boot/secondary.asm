/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/boot/secondary.asm
 * Description: Secondary-core entry for PSCI-brought-up CPUs. NOT boot.asm:
 *              only core 0 reaches _start (QEMU virt holds secondaries
 *              OFF-until-CPU_ON), so the secondaries arrive HERE via PSCI
 *              CPU_ON to &secondary_entry. This is the per-core EL2->EL1 drop
 *              (mirroring boot.asm's drop_to_el1) + per-core SP from the
 *              context_id + the call into C. It deliberately does NOT zero the
 *              BSS region and does NOT stamp the stack-guard sentinel - those are
 *              the primary's job (boot.asm); a secondary doing either would wipe
 *              live kernel state or corrupt the primary's sentinel.
 * ============================================================================ */

    .section .text.boot
    .global secondary_entry

/* ============================================================================
 * secondary_entry - per-core entry, reached only via PSCI CPU_ON
 *
 * On entry (PSCI delivers this):
 *   - CPU in EL2, MMU off, caches off (same cold state as the primary at _start)
 *   - x0 = context_id = this core's stack TOP (smp_init passed it as the CPU_ON
 *     context_id; PSCI hands the context_id to the secondary in x0)
 *
 * What it does, and ONLY this:
 *   1. stash x0 (the stack top) - it is clobbered by the EL2->EL1 drop
 *   2. if at EL2, drop to EL1 mirroring boot.asm drop_to_el1 EXACTLY, including
 *      CNTVOFF_EL2=0 (REQUIRED per core: a nonzero virtual-timer offset makes
 *      this core's CNTVCT wrong - boot.asm zeroes it for the primary at
 *      boot.asm:78-79, each secondary must do the same in its own drop)
 *   3. at EL1, set SP from the stashed stack top, then bl secondary_main
 *
 * What it MUST NOT do (and does not): re-zero the BSS region (the primary's
 * boot.asm already cleared it, incl. the secondary stacks and the online flags -
 * re-clearing would wipe live state), re-stamp the stack-guard sentinel (that is
 * the primary's stack; a secondary has its own and does not participate in the
 * primary's guard), build page tables, or touch the GICD distributor
 * (secondary_main uses vmm_enable_secondary + gic_init_secondary, the
 * per-core-only paths).
 * ============================================================================ */
secondary_entry:
    mov x20, x0                 /* stash context_id = this core's stack top */

    /* Are we at EL2? (QEMU virt brings secondaries up at EL2, like the primary) */
    mrs x0, CurrentEL
    and x0, x0, #0xC            /* extract EL bits [3:2] */
    lsr x0, x0, #2             /* shift to EL number */
    cmp x0, #2
    b.ne sec_el1               /* already EL1: skip the drop */

    /* ---- drop EL2 -> EL1 (mirror of boot.asm drop_to_el1) ---- */
    mov x0, #(1 << 31)          /* HCR_EL2.RW=1: EL1 uses AArch64 */
    orr x0, x0, #(1 << 1)       /* SWIO hardwired on PE */
    msr hcr_el2, x0

    mov x0, #0
    msr cntvoff_el2, x0         /* REQUIRED per core: zero virtual-timer offset */

    mov x0, #3
    msr cnthctl_el2, x0         /* EL1 physical/virtual timer access */

    mov x0, #0
    msr cptr_el2, x0            /* do not trap coprocessor accesses from EL1 */
    isb

    mov x0, #0x3c5              /* SPSR: EL1h, DAIF masked (matches boot.asm:92) */
    msr spsr_el2, x0

    adr x0, sec_el1
    msr elr_el2, x0
    eret                        /* exception return to sec_el1 at EL1 */

/* ============================================================================
 * sec_el1 - now running in EL1 (kernel mode) on this secondary
 * ============================================================================ */
sec_el1:
    mov x0, x20                 /* per-core stack top from the context_id */
    msr spsel, #1              /* use SP_EL1 */
    isb
    mov sp, x0                 /* SP_EL1 = this core's private stack top */

    bl secondary_main           /* C: VBAR + per-core MMU + per-core GICC + park */

    /* secondary_main parks in wfe and should not return; if it ever does, halt. */
sec_halt:
    wfi
    b sec_halt

/* ============================================================================
 * End of secondary.asm
 * ============================================================================ */
