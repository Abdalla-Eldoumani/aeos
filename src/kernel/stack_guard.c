/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/kernel/stack_guard.c
 * Description: Kernel stack-overflow sentinel detect predicate and panic check
 * ============================================================================ */

#include <aeos/stack_guard.h>
#include <aeos/kprintf.h>
#include <aeos/symbols.h>

/* No MMU until Phase 4, so a faulting guard page is unavailable. The boot stack
 * instead carries a magic word at its bottom (__stack_limit, stamped by
 * boot.asm). Overflow grows the stack pointer down past the bottom and
 * overwrites the word. The check is split so a non-destructive test can read
 * the sentinel without triggering the halt: stack_guard_intact() reads and
 * compares; stack_guard_check() adds the panic-and-halt. */

bool stack_guard_intact(void)
{
    /* volatile so the read is not hoisted or folded away; the sentinel is
     * written once at boot and only ever changes through stack corruption. */
    return *(volatile uint64_t *)&__stack_limit == STACK_GUARD_MAGIC;
}

void stack_guard_check(uint64_t pc)
{
    char pc_name[96];

    if (stack_guard_intact()) {
        return;
    }

    /* Overflow detected. Name the offending PC, symbolize it when known, then
     * halt with all interrupts masked so the corrupt stack is not used again. */
    klog_fatal("kernel stack overflow detected, PC=%p", (void *)pc);
    symbol_lookup(pc, pc_name, sizeof(pc_name));
    klog_fatal("PC sym: %s", pc_name);

    __asm__ volatile("msr DAIFSet, #0xF" ::: "memory");
    while (1) {
        __asm__ volatile("wfi");
    }
}

/* ============================================================================
 * End of stack_guard.c
 * ============================================================================ */
