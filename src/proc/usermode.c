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
 * End of usermode.c
 * ============================================================================ */
