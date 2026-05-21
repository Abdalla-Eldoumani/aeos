/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/usermode.h
 * Description: One-shot EL0 entry/return trampoline. Drops to EL0 via eret and
 *              returns to the kernel by abandoning the svc exception frame.
 * ============================================================================ */

#ifndef AEOS_USERMODE_H
#define AEOS_USERMODE_H

#include <aeos/types.h>

/**
 * Enter EL0 once and run the payload at entry with SP_EL0 = user_sp.
 *
 * Saves the kernel's callee-saved state (x19-x30, FP, LR, SP) into a file-static
 * one-shot area, sets a one-shot-active flag, then sets ELR_EL1 = entry,
 * SP_EL0 = user_sp, SPSR_EL1 = 0x3C0 (EL0t with DAIF masked) and erets to EL0.
 *
 * This is a ONE-SHOT, single-CPU, non-nesting transition: DAIF is masked across
 * the whole EL0 run (no preemption of the payload), and there is exactly one
 * saved kernel context. The function does NOT return by normal control flow -
 * control comes back through usermode_return (driven by the EL0 exit syscall).
 * user_sp must be a mapped, 16-byte-aligned EL0 stack top.
 */
void usermode_enter(uint64_t entry, uint64_t user_sp) __attribute__((noreturn));

/**
 * Return control to the kernel from inside the EL0 svc handler. Clears the
 * one-shot flag, restores the saved kernel callee-saved state and SP, and ret's
 * into usermode_enter's caller. This deliberately abandons the EL1 svc exception
 * frame so control does NOT eret back to EL0. Call only while a one-shot is active.
 */
void usermode_return(void) __attribute__((noreturn));

/**
 * True while an EL0 one-shot is in flight (between usermode_enter's eret and
 * usermode_return). The EL0 exit syscall branches on this so a real kernel
 * thread's exit still takes the normal process_exit path.
 */
bool el0_oneshot_active(void);

#endif /* AEOS_USERMODE_H */
