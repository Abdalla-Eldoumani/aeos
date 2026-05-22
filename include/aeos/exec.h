/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/exec.h
 * Description: Static ELF64 loader/runner. elf_exec_file reads a static ELF off
 *              the VFS into a size-bounded kmalloc buffer, validates it, maps
 *              each PT_LOAD into the single EL0 window (USER_TEXT for PF_X,
 *              USER_DATA otherwise) with BSS zero-fill and a computed EL0 stack,
 *              and enters EL0 at e_entry by reusing the Phase 5 usermode_enter
 *              one-shot. The two accessors expose the mapped-window extent for
 *              the syscall user-pointer bound check.
 * ============================================================================ */

#ifndef AEOS_EXEC_H
#define AEOS_EXEC_H

#include <aeos/types.h>
#include <aeos/process.h>

/**
 * Load, validate, map, and run a static ELF64 at EL0, one program at a time.
 *
 * Opens path read-only, reads it into a size-bounded heap buffer, runs
 * elf_validate, maps each PT_LOAD into the [0x80000000, 0xC0000000) EL0 window
 * (PF_X -> USER_TEXT read-only code, otherwise USER_DATA), zero-fills any BSS
 * tail, maps a fresh EL0 stack page ABOVE the highest loaded segment, then
 * enters EL0 at e_entry via the reused usermode_enter one-shot. Returns after
 * the EL0 program exits through the sys_exit one-shot branch.
 *
 * Returns 0 on a completed run, a negative error code on any reject/error.
 * A malformed or non-ELF file (or one with an out-of-bounds/out-of-window
 * segment) is rejected with a logged error and never faults. Only the single
 * EL0 window is supported; one EL0 program runs at a time.
 */
int elf_exec_file(const char *path);

/**
 * Base VA (inclusive) of the currently-mapped EL0 window: 0x80000000 while an
 * EL0 program is loaded. Used by the syscall layer's user-pointer bound check.
 */
uint64_t usermode_map_base(void);

/**
 * Top VA (exclusive) of the currently-mapped EL0 window: the address one past
 * the EL0 stack page that elf_exec_file mapped above the top segment. Valid
 * while an EL0 one-shot is active; used by the syscall user-pointer bound check.
 */
uint64_t usermode_map_end(void);

/**
 * The process_t the loader registered for the EL0 program currently running
 * (set by elf_exec_file for the duration of the run), or NULL when no EL0
 * program is loaded. The syscall layer reads its kill_requested flag at the EL0
 * syscall boundary - this is the LOADED process, NOT process_current() (which is
 * idle during the synchronous one-shot), which is why a dedicated pointer exists.
 */
process_t *current_user_proc_get(void);

#ifdef TEST_BUILD
/**
 * Point the EL0 kill seam at a process directly, bypassing a full elf_exec_file
 * run. test_process_kill_reap registers a user PCB, sets it here, arms its kill
 * flag via process_kill(pid), and enters EL0 with a getpid-first payload so the
 * seam (which reads current_user_proc->kill_requested) reaps the run before the
 * getpid dispatches. The test clears it (NULL) afterward. TEST_BUILD only - the
 * production loader sets/clears current_user_proc itself across usermode_enter.
 */
void current_user_proc_set(process_t *proc);
#endif /* TEST_BUILD */

#endif /* AEOS_EXEC_H */
