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

/**
 * Protection class for an EL0 user page. All classes set PXN=1 (EL1 must never
 * execute user memory). They differ in AP[2:1] and UXN:
 *   USER_EXEC - AP=01 (EL0 RW), UXN=0: EL0 may fetch-and-execute AND write the
 *               page. Kept for the Phase 5 one-shot payloads, which map a
 *               compile-time code page in place and need it EL0-writable; NOT
 *               W^X.
 *   USER_DATA - AP=01 (EL0 RW), UXN=1: the page is never executed at any EL
 *               (a stack/data page).
 *   USER_TEXT - AP=11 (EL0 RO), UXN=0: EL0 may fetch-and-execute but NOT write
 *               the page (a loaded code segment). This is real per-segment W^X
 *               for loaded code: PF_X ELF segments are mapped USER_TEXT so the
 *               EL0 program cannot rewrite its own instructions. The kernel
 *               itself still runs from one coarse RWX 1GB block (kernel-wide
 *               W^X is out of scope).
 */
typedef enum { USER_EXEC, USER_DATA, USER_TEXT } user_prot_t;

/**
 * Map a single 4KB EL0-accessible page at user VA uva to physical page pa with
 * the given protection. Builds an L1->L2->L3 walk chain at the FREE L1 index for
 * uva (only the single VA 0x80000000 1GB window is supported this phase: index 2,
 * which the Phase 4 identity map leaves invalid). The kernel RAM block (L1 index
 * 1) and MMIO block (index 0) are NOT touched, so EL0 still faults on every kernel
 * VA via their AP=00. Ends with dsb ish; tlbi vmalle1; dsb ish; isb so the live
 * table walk sees the new entries and the I-side is coherent for a code page.
 *
 * uva and pa must be 4KB aligned. Re-calling for a second VA in the same window
 * (e.g. a stack page) reuses the same L2/L3 tables.
 */
void vmm_map_user_page(uint64_t uva, uint64_t pa, user_prot_t prot);

#endif /* AEOS_VMM_H */
