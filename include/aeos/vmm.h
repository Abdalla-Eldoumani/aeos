/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/vmm.h
 * Description: Virtual memory manager - MMU bringup (identity low map +
 *              TTBR1 high-half kernel alias) for the ARMv8-A MMU at EL1
 * ============================================================================ */

#ifndef AEOS_VMM_H
#define AEOS_VMM_H

#include <aeos/types.h>

/* TTBR1 high-half canonical base for a 39-bit VA (T1SZ=25, top 25 bits all 1).
 * The kernel's high-half alias of physical PA lives at VMM_TTBR1_BASE + PA. */
#define VMM_TTBR1_BASE  0xFFFFFF8000000000ULL

/**
 * Build the level-1 identity tables (RAM Normal + low MMIO Device) plus the
 * TTBR1 high-half alias, then enable the MMU and the data/instruction caches.
 * The running PC is inside the identity-mapped RAM block, so execution
 * continues across the enable with no jump. Call once, early in kernel_main.
 */
void vmm_init(void);

/**
 * Print the headless proof to serial: one klog_info line containing the
 * literal token "MMU enabled" with the read-back SCTLR_EL1.M/C/I bits, and a
 * second line confirming the TTBR1 high-half alias of _kernel_start matches
 * the identity read.
 */
void vmm_report(void);

/**
 * Read the 32-bit word that the TTBR1 high-half alias maps to physical pa
 * (i.e. *(VMM_TTBR1_BASE + pa)). For any pa inside mapped RAM this equals the
 * identity read at pa. Used by the MMU smoke scenario in test_runner.
 */
uint32_t vmm_ttbr1_alias_read(uint64_t pa);

#endif /* AEOS_VMM_H */
