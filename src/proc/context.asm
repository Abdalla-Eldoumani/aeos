/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/proc/context.asm
 * Description: Context switching for cooperative multitasking
 * ============================================================================ */

include(`src/boot/macros.m4')

/* ============================================================================
 * Context Switch Function
 *
 * void context_switch(process_t *from, process_t *to);
 *
 * Saves the current process context and restores the next process context.
 *
 * Arguments:
 *   x0 = from (pointer to current process PCB)
 *   x1 = to   (pointer to next process PCB)
 *
 * Process PCB layout (from include/aeos/process.h):
 *   Offset  Field
 *   0x00    pid (uint64_t)
 *   0x08    state (uint32_t) + padding
 *   0x10    name (const char *)
 *   0x18    x19
 *   0x20    x20
 *   0x28    x21
 *   0x30    x22
 *   0x38    x23
 *   0x40    x24
 *   0x48    x25
 *   0x50    x26
 *   0x58    x27
 *   0x60    x28
 *   0x68    x29 (FP)
 *   0x70    x30 (LR)
 *   0x78    sp
 * ============================================================================ */

    .section .text
    .global context_switch
    .balign 4

context_switch:
    /* Save current process context (from = x0) */
    /* We only save callee-saved registers per ARM64 ABI */
    stp x19, x20, [x0, #0x18]   /* Save x19, x20 */
    stp x21, x22, [x0, #0x28]   /* Save x21, x22 */
    stp x23, x24, [x0, #0x38]   /* Save x23, x24 */
    stp x25, x26, [x0, #0x48]   /* Save x25, x26 */
    stp x27, x28, [x0, #0x58]   /* Save x27, x28 */
    stp x29, x30, [x0, #0x68]   /* Save x29 (FP), x30 (LR) */

    /* Save current stack pointer */
    mov x2, sp
    str x2, [x0, #0x78]         /* Save SP */

    /* Restore next process context (to = x1) */
    ldp x19, x20, [x1, #0x18]   /* Restore x19, x20 */
    ldp x21, x22, [x1, #0x28]   /* Restore x21, x22 */
    ldp x23, x24, [x1, #0x38]   /* Restore x23, x24 */
    ldp x25, x26, [x1, #0x48]   /* Restore x25, x26 */
    ldp x27, x28, [x1, #0x58]   /* Restore x27, x28 */
    ldp x29, x30, [x1, #0x68]   /* Restore x29 (FP), x30 (LR) */

    /* Restore next process's stack pointer */
    ldr x2, [x1, #0x78]         /* Load SP */
    mov sp, x2

    /* Return to next process */
    /* This will return to wherever the next process was when it called yield() */
    /* or to the process entry point if this is the first time it's being run */
    ret

/* ============================================================================
 * End of context.asm
 * ============================================================================ */
