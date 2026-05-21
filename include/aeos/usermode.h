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
 * saved kernel context. By normal control flow it does not come back here; the
 * EL0 exit syscall drives usermode_return, which restores the saved context and
 * ret's to usermode_enter's caller (so the caller DOES resume, just after a
 * detour through EL0). It is therefore deliberately NOT marked noreturn: the
 * caller must keep a live epilogue and return address for usermode_return's ret
 * to land on. user_sp must be a mapped, 16-byte-aligned EL0 stack top.
 */
void usermode_enter(uint64_t entry, uint64_t user_sp);

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

/**
 * One-shot EL0 payload selector for usermode_run_payload.
 *   USERMODE_PAYLOAD_ROUNDTRIP - svc SYS_GETPID (observable) then svc SYS_EXIT.
 *   USERMODE_PAYLOAD_PRIV_TRAP - msr daifset (traps EC=0x18) then a fail-path exit.
 */
typedef enum {
    USERMODE_PAYLOAD_ROUNDTRIP,
    USERMODE_PAYLOAD_PRIV_TRAP
} usermode_payload_t;

/**
 * Map the selected naked payload at VA 0x80000000 (USER_EXEC) and a fresh
 * pmm-backed stack page at 0x80001000 (USER_DATA), then usermode_enter at the
 * payload entry with SP_EL0 = 0x80002000. The single map+enter recipe lives
 * here so both callers (test_runner and main.c) stay thin. Returns by way of
 * usermode_return once the payload's exit (or the trap seam) hands control back.
 */
void usermode_run_payload(usermode_payload_t kind);

/**
 * EL0 lower-EL synchronous handler for the non-SVC path of el0_aarch64_sync.
 * Declared unconditionally because the vector calls it in every build. When the
 * test trap-capture is armed (TEST_BUILD only), it records the ESR_EL1 EC and
 * returns to the kernel via usermode_return; otherwise it forwards to the
 * halting handle_exception, so production EL0 faults still panic as before.
 */
void handle_el0_sync(uint32_t source, uint32_t type, void *ctx);

#ifdef TEST_BUILD
/**
 * Arm the one-shot trap-capture seam: the next non-SVC EL0 sync exception is
 * recorded (its ESR_EL1 EC stored, a captured flag set) and control returns to
 * the kernel instead of halting. Compiled only under TEST_BUILD; production
 * builds never arm it, so handle_el0_sync always forwards to handle_exception.
 */
void usermode_arm_trap_capture(void);

/**
 * The ESR_EL1 exception class (EC, bits [31:26]) recorded by the armed seam.
 * Valid only after usermode_trap_was_captured() returns true.
 */
uint32_t usermode_captured_ec(void);

/**
 * True once the armed seam has recorded a trapped EL0 exception.
 */
bool usermode_trap_was_captured(void);
#endif /* TEST_BUILD */

#endif /* AEOS_USERMODE_H */
