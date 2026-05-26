/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/scheduler.h
 * Description: Process scheduler interface
 * ============================================================================ */

#ifndef AEOS_SCHEDULER_H
#define AEOS_SCHEDULER_H

#include <aeos/process.h>

/**
 * Initialize the scheduler
 * Sets up the idle process and ready queue
 */
void scheduler_init(void);

/**
 * Add a process to the scheduler's ready queue
 *
 * @param proc Process to add
 */
void scheduler_add_process(process_t *proc);

/**
 * Remove a process from the scheduler
 *
 * @param proc Process to remove
 */
void scheduler_remove_process(process_t *proc);

/**
 * Select the next process to run
 * Uses round-robin algorithm
 *
 * @return Next process to execute
 */
process_t *schedule(void);

/**
 * Yield CPU to the next process
 * Cooperative context switch - current process voluntarily gives up CPU
 * This is the main entry point for cooperative multitasking
 */
void yield(void);

/**
 * Timer tick handler for preemptive scheduling
 * Called from timer IRQ handler to decrement time slices
 * and trigger context switches when a process exhausts its quantum
 */
void scheduler_tick(void);

/**
 * Start the scheduler
 * Begins executing the first process in the ready queue
 * This function does not return
 */
void scheduler_start(void) __attribute__((noreturn));

/**
 * Get scheduler statistics
 */
typedef struct {
    uint64_t total_processes;       /* Total processes created */
    uint64_t running_processes;     /* Currently active processes */
    uint64_t context_switches;      /* Total context switches */
} scheduler_stats_t;

void scheduler_get_stats(scheduler_stats_t *stats);

#ifdef TEST_BUILD
/*
 * Read-only access to the file-static scheduler.current (the actual target that
 * scheduler_tick increments). test_ps_accounting reads it, ticks N times, and
 * asserts its total_time advanced by N - the ticks-are-real proof. There is no
 * way to make an arbitrary test PCB scheduler.current (process_set_current sets
 * a DIFFERENT variable, schedule() returns idle on an empty ready_head, and
 * scheduler_start() is noreturn), so the assertion must target scheduler.current
 * itself. Mirrors the editor_test_ and syscall_test_ TEST seams; compiled out of
 * production (no prod symbol, no -Werror unused warning).
 */
process_t *scheduler_test_current(void);
#endif /* TEST_BUILD */

#endif /* AEOS_SCHEDULER_H */
