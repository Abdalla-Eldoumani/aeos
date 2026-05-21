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

/* Single-CPU one-shot save area. Layout matches the stp/ldp offsets below:
 * x19..x28 at bytes 0..72, x29(FP) at 80, x30(LR) at 88, SP at 96. There is no
 * nesting (one EL0 run at a time, DAIF masked across it), so one static is enough. */
static struct {
    uint64_t x19_28[10];
    uint64_t fp;
    uint64_t lr;
    uint64_t sp;
} kctx;

/* Set true between the eret to EL0 and usermode_return. The EL0 exit syscall
 * reads it to decide between usermode_return (one-shot) and process_exit. */
static bool el0_oneshot;

void usermode_enter(uint64_t entry, uint64_t user_sp)
{
    /* Save callee-saved + FP/LR + SP so usermode_return can resume this CALLER. */
    __asm__ volatile(
        "stp x19, x20, [%0, #0]\n"
        "stp x21, x22, [%0, #16]\n"
        "stp x23, x24, [%0, #32]\n"
        "stp x25, x26, [%0, #48]\n"
        "stp x27, x28, [%0, #64]\n"
        "stp x29, x30, [%0, #80]\n"
        "mov x1, sp\n"
        "str x1, [%0, #96]\n"
        :: "r"(&kctx) : "x1", "memory");

    el0_oneshot = true;
    klog_info("entered EL0: entry=%p sp=%p", (void *)entry, (void *)user_sp);

    /* Drop to EL0t with DAIF masked (0x3C0). The eret returns to EL0 because of
     * this SPSR, not because of anything in the vector handler. */
    __asm__ volatile(
        "msr elr_el1, %0\n"
        "msr sp_el0, %1\n"
        "mov x2, #0x3C0\n"
        "msr spsr_el1, x2\n"
        "isb\n"
        "eret\n"
        :: "r"(entry), "r"(user_sp) : "x2", "memory");

    __builtin_unreachable();
}

void usermode_return(void)
{
    klog_info("returned to kernel");
    el0_oneshot = false;

    /* Restore the saved kernel context and ret into usermode_enter's caller.
     * The ret lands on the saved LR, so the EL1 svc exception frame still on the
     * stack is simply abandoned - safe for a single one-shot on a single CPU. */
    __asm__ volatile(
        "ldp x19, x20, [%0, #0]\n"
        "ldp x21, x22, [%0, #16]\n"
        "ldp x23, x24, [%0, #32]\n"
        "ldp x25, x26, [%0, #48]\n"
        "ldp x27, x28, [%0, #64]\n"
        "ldp x29, x30, [%0, #80]\n"
        "ldr x1, [%0, #96]\n"
        "mov sp, x1\n"
        "ret\n"
        :: "r"(&kctx) : "x1", "memory");

    __builtin_unreachable();
}

bool el0_oneshot_active(void)
{
    return el0_oneshot;
}

/* ============================================================================
 * EL0 payloads (RESEARCH Pattern 4 / Code Examples). Defined as module-level
 * asm rather than naked C functions: AArch64 GCC ignores __attribute__((naked))
 * on a function with a body (-Werror=attributes), so the payloads live in their
 * own .text block with no compiler-generated prologue. usermode_run_payload
 * maps the 4KB page holding the chosen one at VA 0x80000000 so EL0 can fetch it.
 * x8 carries the syscall number; svc #0 traps into el0_aarch64_sync. The symbols
 * are local (no .global); C takes their addresses via the externs below.
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
    "el0_roundtrip:\n"
    "    mov x8, #3\n"        /* SYS_GETPID */
    "    svc #0\n"
    "    mov x8, #0\n"        /* SYS_EXIT */
    "    svc #0\n"
    "0:  b 0b\n"
    ".balign 64\n"
    "el0_priv_trap:\n"
    "    msr daifset, #2\n"   /* traps to EL1, EC=0x18 */
    "    mov x8, #0\n"        /* SYS_EXIT - only reached if the msr did NOT trap */
    "    svc #0\n"
    "1:  b 1b\n");

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
