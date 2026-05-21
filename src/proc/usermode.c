/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/proc/usermode.c
 * Description: One-shot EL0 entry/return trampoline. usermode_enter saves the
 *              kernel's callee-saved state and erets to EL0; usermode_return
 *              restores it and ret's into the caller, bypassing the svc vector
 *              tail's eret so control truly comes back to the kernel. Kept out
 *              of context.asm (the do-not-touch EL1<->EL1 switch) on purpose.
 * ============================================================================ */

#include <aeos/usermode.h>
#include <aeos/kprintf.h>
#include <aeos/vmm.h>
#include <aeos/pmm.h>
#include <aeos/interrupts.h>
#include <aeos/types.h>

/* Single-CPU one-shot save area. Layout matches the stp/ldp offsets in the asm
 * trampoline below: x19..x28 at bytes 0..72, x29(FP) at 80, x30(LR) at 88, SP at
 * 96. There is no nesting (one EL0 run at a time, DAIF masked across it), so one
 * static is enough. The struct is referenced by name from the top-level asm. */
struct usermode_kctx {
    uint64_t x19_28[10];
    uint64_t fp;
    uint64_t lr;
    uint64_t sp;
};
struct usermode_kctx usermode_kctx;

/* Set true between the eret to EL0 and usermode_return. The EL0 exit syscall
 * reads it to decide between usermode_return (one-shot) and process_exit. */
static bool el0_oneshot;

bool el0_oneshot_active(void)
{
    return el0_oneshot;
}

/* Small C helpers the asm trampoline calls so the flag-set and the klog markers
 * stay in C. usermode_enter saves the caller's regs, then bl's the log helper,
 * then erets; usermode_return bl's the log helper, then restores and ret's. */
void usermode_enter_log(uint64_t entry, uint64_t user_sp)
{
    el0_oneshot = true;
    klog_info("entered EL0: entry=%p sp=%p", (void *)entry, (void *)user_sp);
}

void usermode_return_log(void)
{
    klog_info("returned to kernel");
    el0_oneshot = false;
}

/* usermode_enter / usermode_return as a top-level naked asm pair. A normal C
 * function would emit a prologue that allocates a frame, so capturing "sp" inside
 * it would record the function's OWN frame, not the caller's; usermode_return's
 * "mov sp; ret" would then resume the caller with a stale sp and corrupt its
 * epilogue (the bug the first cut had). With no compiler prologue, the saved sp/
 * lr are exactly the caller's (usermode_run_payload's) call-boundary state, so
 * usermode_return's ret unwinds cleanly back into the caller, frame intact.
 *
 * usermode_enter(x0=entry, x1=user_sp): save x19-x28/x29/x30/sp into
 * usermode_kctx, log via usermode_enter_log (entry/sp preserved in x19/x20 across
 * the call), then ELR_EL1=entry, SP_EL0=user_sp, SPSR_EL1=0x3C0 (EL0t, DAIF
 * masked), eret. The eret returns to EL0 because of the SPSR, not the handler.
 * usermode_return(): log, then restore x19-x28/x29/x30/sp and ret to the saved
 * caller LR. The EL1 svc exception frame still on the stack is abandoned - safe
 * for a single non-nesting one-shot on a single CPU. */
__asm__(
    ".section .text\n"
    ".balign 4\n"
    ".global usermode_enter\n"
    ".type usermode_enter, %function\n"
    "usermode_enter:\n"
    "    adrp x9, usermode_kctx\n"
    "    add  x9, x9, :lo12:usermode_kctx\n"
    "    stp  x19, x20, [x9, #0]\n"
    "    stp  x21, x22, [x9, #16]\n"
    "    stp  x23, x24, [x9, #32]\n"
    "    stp  x25, x26, [x9, #48]\n"
    "    stp  x27, x28, [x9, #64]\n"
    "    stp  x29, x30, [x9, #80]\n"   /* caller FP + caller return address */
    "    mov  x10, sp\n"               /* no prologue ran, so sp == caller's sp */
    "    str  x10, [x9, #96]\n"
    "    mov  x19, x0\n"               /* preserve entry across the log call */
    "    mov  x20, x1\n"               /* preserve user_sp */
    "    bl   usermode_enter_log\n"
    "    msr  elr_el1, x19\n"
    "    msr  sp_el0, x20\n"
    "    mov  x2, #0x3C0\n"            /* EL0t, DAIF masked */
    "    msr  spsr_el1, x2\n"
    "    isb\n"
    "    eret\n"
    ".size usermode_enter, .-usermode_enter\n"

    ".balign 4\n"
    ".global usermode_return\n"
    ".type usermode_return, %function\n"
    "usermode_return:\n"
    "    bl   usermode_return_log\n"
    "    adrp x9, usermode_kctx\n"
    "    add  x9, x9, :lo12:usermode_kctx\n"
    "    ldp  x19, x20, [x9, #0]\n"
    "    ldp  x21, x22, [x9, #16]\n"
    "    ldp  x23, x24, [x9, #32]\n"
    "    ldp  x25, x26, [x9, #48]\n"
    "    ldp  x27, x28, [x9, #64]\n"
    "    ldp  x29, x30, [x9, #80]\n"   /* restore caller FP + caller return addr */
    "    ldr  x10, [x9, #96]\n"
    "    mov  sp, x10\n"               /* back to caller's call-boundary sp */
    "    ret\n"                        /* unwind into usermode_run_payload */
    ".size usermode_return, .-usermode_return\n");

/* ============================================================================
 * EL0 payloads (RESEARCH Pattern 4 / Code Examples). Defined as module-level
 * asm rather than naked C functions: AArch64 GCC ignores __attribute__((naked))
 * on a function with a body (-Werror=attributes), so the payloads live in their
 * own .text block with no compiler-generated prologue. usermode_run_payload
 * maps the 4KB page holding the chosen one at VA 0x80000000 so EL0 can fetch it.
 * x8 carries the syscall number; svc #0 traps into el0_aarch64_sync. The symbols
 * are .global so the C extern references below resolve to their real linked
 * addresses: when they were local, the compiler collapsed both &el0_roundtrip
 * and &el0_priv_trap to a single literal-pool slot that pointed at the wrong
 * symbol (the nearest preceding global), so usermode_run_payload mapped and
 * eret'd to garbage and the EL0 payload took a data abort.
 * ============================================================================ */

/* el0_roundtrip: SYS_GETPID (observable side effect) then SYS_EXIT (returns to
 * the kernel via the sys_exit_impl one-shot branch -> usermode_return). The
 * trailing spin guards against fallthrough; it is never reached.
 * el0_priv_trap: writing the DAIF mask from EL0 traps to EL1 with ESR EC=0x18
 * (SCTLR_EL1.UMA=0, the reset value the kernel never changes). If it did NOT
 * trap, the fail-path SYS_EXIT keeps the runner from hanging. */
__asm__(
    ".section .text\n"
    ".balign 64\n"
    ".global el0_roundtrip\n"
    ".type el0_roundtrip, %function\n"
    "el0_roundtrip:\n"
    "    mov x8, #3\n"        /* SYS_GETPID */
    "    svc #0\n"
    "    mov x8, #0\n"        /* SYS_EXIT */
    "    svc #0\n"
    "0:  b 0b\n"
    ".size el0_roundtrip, .-el0_roundtrip\n"
    ".balign 64\n"
    ".global el0_priv_trap\n"
    ".type el0_priv_trap, %function\n"
    "el0_priv_trap:\n"
    "    msr daifset, #2\n"   /* traps to EL1, EC=0x18 */
    "    mov x8, #0\n"        /* SYS_EXIT - only reached if the msr did NOT trap */
    "    svc #0\n"
    "1:  b 1b\n"
    ".size el0_priv_trap, .-el0_priv_trap\n");

/* Symbols defined by the asm block above; declared as functions so C can take
 * their addresses. They are never called directly from C. */
extern void el0_roundtrip(void);
extern void el0_priv_trap(void);

/* ============================================================================
 * Map + enter helper. One place owns the RESEARCH Pattern 4 mapping recipe so
 * both callers (test_runner, main.c) stay thin.
 * ============================================================================ */

void usermode_run_payload(usermode_payload_t kind)
{
    /* Pick the payload by kind. Its PA equals its VA under the kernel identity
     * map; we map the 4KB page that contains it and offset the entry. */
    uint64_t pa = (kind == USERMODE_PAYLOAD_PRIV_TRAP)
                ? (uint64_t)&el0_priv_trap
                : (uint64_t)&el0_roundtrip;
    uint64_t page_pa    = pa & ~0xFFFULL;
    uint64_t user_entry = 0x80000000ULL + (pa & 0xFFFULL);

    /* Code page at VA 0x80000000 (USER_EXEC: AP=01, UXN=0, PXN=1). */
    vmm_map_user_page(0x80000000ULL, page_pa, USER_EXEC);

    /* Fresh EL0 stack page at VA 0x80001000 (USER_DATA: AP=01, UXN=1, PXN=1).
     * SP_EL0 starts at the top of that page (0x80002000). */
    uint64_t stack_pa = pmm_alloc_page();
    if (stack_pa == 0) {
        /* Should not happen this early; do not eret without a mapped stack. */
        klog_error("usermode_run_payload: pmm_alloc_page failed, EL0 entry skipped");
        return;
    }
    vmm_map_user_page(0x80001000ULL, stack_pa, USER_DATA);

    usermode_enter(user_entry, 0x80002000ULL);
}

/* ============================================================================
 * Non-halting EL0 trap-record seam. The vector's non-SVC branch calls
 * handle_el0_sync. Under TEST_BUILD a scenario can arm el0_expect_trap so the
 * next trapped EL0 exception is recorded and control returns to the kernel
 * (not the halting handle_exception). Production never arms it.
 * ============================================================================ */

#ifdef TEST_BUILD
static bool     el0_expect_trap;
static bool     el0_trap_captured;
static uint32_t el0_captured_ec;

void usermode_arm_trap_capture(void)
{
    el0_expect_trap   = true;
    el0_trap_captured = false;
    el0_captured_ec   = 0;
}

uint32_t usermode_captured_ec(void)
{
    return el0_captured_ec;
}

bool usermode_trap_was_captured(void)
{
    return el0_trap_captured;
}
#endif /* TEST_BUILD */

void handle_el0_sync(uint32_t source, uint32_t type, void *ctx)
{
#ifdef TEST_BUILD
    if (el0_expect_trap) {
        /* Record the EC and return to the kernel instead of halting. */
        uint64_t esr = get_exception_syndrome();
        el0_captured_ec   = (uint32_t)((esr >> 26) & 0x3F);
        el0_trap_captured = true;
        el0_expect_trap   = false;
        usermode_return();  /* never returns; abandons the EL0 svc/exc frame */
    }
#endif
    /* Not armed (or a production build): preserve the halting fault path so a
     * genuine EL0 fault still panics with the full ESR/FAR/PC decode. */
    handle_exception(source, type, (cpu_context_t *)ctx);
}

/* ============================================================================
 * End of usermode.c
 * ============================================================================ */
