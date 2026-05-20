/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/stack_guard.h
 * Description: Kernel stack-overflow sentinel: detect predicate and panic check
 * ============================================================================ */

#ifndef AEOS_STACK_GUARD_H
#define AEOS_STACK_GUARD_H

#include <aeos/types.h>

/* Sentinel word stamped at __stack_limit (the bottom of the boot stack) by
 * boot.asm. A stack that grows past its bottom overwrites this word, so a
 * mismatch means overflow. The value MUST match the immediate boot.asm stores. */
#define STACK_GUARD_MAGIC 0xAE057ACCu

/* Bottom (lowest address) of the .stack region, defined by linker.ld. The C
 * code reads the sentinel through its address; it is not an lvalue to load. */
extern uint64_t __stack_limit;

/**
 * Non-halting overflow predicate. Reads the sentinel at __stack_limit and
 * returns whether it still equals STACK_GUARD_MAGIC. Read-and-compare only,
 * no side effects, so the security smoke test can corrupt the sentinel, call
 * this, and restore it without halting the kernel.
 */
bool stack_guard_intact(void);

/**
 * Production overflow check. If the sentinel is intact this returns at once.
 * Otherwise it issues a klog_fatal naming `pc` (the offending PC at exception
 * entry, or 0 at a timer tick where there is no faulting instruction), masks
 * DAIF, and halts in a wfi loop. Allocation-free and reentrant-safe so it can
 * run on the FIQ tick path.
 */
void stack_guard_check(uint64_t pc);

#endif /* AEOS_STACK_GUARD_H */
