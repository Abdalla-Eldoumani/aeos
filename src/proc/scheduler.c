/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/proc/scheduler.c
 * Description: Cooperative round-robin scheduler
 * ============================================================================ */

#include <aeos/scheduler.h>
#include <aeos/process.h>
#include <aeos/kprintf.h>
#include <aeos/uart.h>
#include <aeos/types.h>

/* External context switch function (from context.asm) */
extern void context_switch(process_t *from, process_t *to);

/* Scheduler state */
static struct {
    process_t *current;         /* Currently running process */
    process_t *idle;            /* Idle process (runs when queue empty) */
    process_t *ready_head;      /* Head of ready queue */
    process_t *ready_tail;      /* Tail of ready queue */
    uint64_t total_processes;
    uint64_t running_processes;
    uint64_t context_switches;
    bool initialized;
} scheduler;

/**
 * Idle process - runs when no other process is ready
 */
static void idle_process(void)
{
    klog_info("Idle process started");

    while (1) {
        /* Just wait for interrupts (low power) */
        __asm__ volatile("wfi");

        /* Since we don't have timer interrupts yet,
         * we'll just yield periodically
         */
        yield();
    }
}

/**
 * Initialize the scheduler
 */
void scheduler_init(void)
{
    klog_info("Initializing scheduler...");
    kprintf("Step 1\n");

    /* Clear scheduler state */
    scheduler.current = NULL;
    scheduler.idle = NULL;
    scheduler.ready_head = NULL;
    scheduler.ready_tail = NULL;
    scheduler.total_processes = 0;
    scheduler.running_processes = 0;
    scheduler.context_switches = 0;
    scheduler.initialized = false;  /* Set to false until fully initialized */

    kprintf("Step 2\n");
    /* Create idle process - this will add it to the ready queue */
    process_t *idle = process_create(idle_process, "idle");
    kprintf("Step 3\n");
    if (idle == NULL) {
        klog_fatal("Failed to create idle process");
        while (1) {
            __asm__ volatile("wfi");
        }
    }

    kprintf("Step 4\n");
    /* Remove idle from ready queue since it's a special process
     * It will only run when no other processes are ready
     */
    scheduler_remove_process(idle);

    kprintf("Step 5\n");
    /* Store idle process reference */
    scheduler.idle = idle;

    /* Set idle as initial current process */
    scheduler.current = idle;
    idle->state = PROCESS_RUNNING;
    process_set_current(idle);

    kprintf("Step 6\n");
    /* Now we're initialized */
    scheduler.initialized = true;

    klog_info("Scheduler initialized (idle process PID=%u)", (uint32_t)idle->pid);
}

/**
 * Add a process to the ready queue
 */
void scheduler_add_process(process_t *proc)
{
    kprintf("    scheduler_add_process: entry\n");

    if (proc == NULL) {
        kprintf("    NULL process, returning\n");
        return;
    }

    kprintf("    Adding PID=%u '%s'\n", (uint32_t)proc->pid, proc->name);

    /* Add to tail of ready queue (circular list) */
    kprintf("    Setting next: ");
    proc->next = NULL;
    kprintf("N ");

    if (scheduler.ready_head == NULL) {
        /* First process in queue */
        kprintf("(first)");
        scheduler.ready_head = proc;
        kprintf("H");
        scheduler.ready_tail = proc;
        kprintf("T");
    } else {
        /* Add to tail */
        kprintf("(tail)");
        scheduler.ready_tail->next = proc;
        kprintf("L");
        scheduler.ready_tail = proc;
        kprintf("T");
    }
    kprintf("\n");

    kprintf("    Setting state & counters: ");
    proc->state = PROCESS_READY;
    kprintf("S");
    scheduler.total_processes++;
    kprintf("t");
    scheduler.running_processes++;
    kprintf("r\n");

    klog_debug("Added process PID=%u '%s' to ready queue",
               (uint32_t)proc->pid, proc->name);
}

/**
 * Remove a process from the scheduler
 */
void scheduler_remove_process(process_t *proc)
{
    process_t *current, *prev;

    if (proc == NULL || scheduler.ready_head == NULL) {
        return;
    }

    /* Find and remove from ready queue */
    prev = NULL;
    for (current = scheduler.ready_head; current != NULL; current = current->next) {
        if (current == proc) {
            /* Found it */
            if (prev == NULL) {
                /* Removing head */
                scheduler.ready_head = current->next;
                if (scheduler.ready_head == NULL) {
                    scheduler.ready_tail = NULL;
                }
            } else {
                /* Removing middle or tail */
                prev->next = current->next;
                if (current == scheduler.ready_tail) {
                    scheduler.ready_tail = prev;
                }
            }

            current->next = NULL;
            scheduler.running_processes--;

            klog_debug("Removed process PID=%u '%s' from scheduler",
                       (uint32_t)proc->pid, proc->name);
            return;
        }
        prev = current;
    }
}

/**
 * Select the next process to run (round-robin)
 */
process_t *schedule(void)
{
    process_t *next;

    if (scheduler.ready_head == NULL) {
        /* No ready processes, return idle process */
        klog_debug("schedule: ready queue empty, returning idle process");
        return scheduler.idle;
    }

    /* Simple round-robin: take first process from ready queue */
    next = scheduler.ready_head;

    /* If current process is still ready, add it to back of queue */
    if (scheduler.current != NULL &&
        scheduler.current->state == PROCESS_RUNNING) {

        scheduler.current->state = PROCESS_READY;

        /* Remove from wherever it is and add to tail */
        scheduler_remove_process(scheduler.current);
        scheduler_add_process(scheduler.current);
    }

    /* Remove next process from ready queue */
    scheduler.ready_head = next->next;
    if (scheduler.ready_head == NULL) {
        scheduler.ready_tail = NULL;
    }
    next->next = NULL;

    return next;
}

/**
 * Yield CPU to next process (cooperative multitasking)
 */
void yield(void)
{
    process_t *from, *to;

    if (!scheduler.initialized) {
        klog_error("yield: Scheduler not initialized");
        return;
    }

    /* Get next process */
    to = schedule();

    if (to == NULL) {
        klog_error("yield: No process to schedule!");
        return;
    }

    from = scheduler.current;

    /* If switching to same process, nothing to do */
    if (from == to) {
        return;
    }

    /* Update states */
    if (from != NULL) {
        if (from->state == PROCESS_RUNNING) {
            from->state = PROCESS_READY;
        }
    }

    to->state = PROCESS_RUNNING;
    scheduler.current = to;
    process_set_current(to);

    /* Update statistics */
    scheduler.context_switches++;

    klog_debug("Context switch: %s (PID %u) -> %s (PID %u)",
               from ? from->name : "NULL", from ? (uint32_t)from->pid : 0,
               to->name, (uint32_t)to->pid);

    /* Perform actual context switch */
    if (from != NULL) {
        context_switch(from, to);
    } else {
        /* First time scheduling, just jump to new process */
        /* This is tricky - we need to manually set up the context */
        __asm__ volatile(
            "mov sp, %0\n"
            "mov x30, %1\n"
            "ret\n"
            :
            : "r"(to->sp), "r"(to->x30)
            : "memory"
        );
    }
}

/**
 * Timer tick handler for preemptive scheduling
 * Called from timer IRQ to manage time slices
 */
void scheduler_tick(void)
{
    /* Don't do anything if scheduler not ready */
    if (!scheduler.initialized || scheduler.current == NULL) {
        return;
    }

    /* Decrement time slice of current process */
    if (scheduler.current->time_slice > 0) {
        scheduler.current->time_slice--;
    }

    /* Track total CPU time */
    scheduler.current->total_time++;

    /* If time slice expired and there are other ready processes, preempt */
    if (scheduler.current->time_slice == 0 && scheduler.ready_head != NULL) {
        /* Reset time slice for next run */
        scheduler.current->time_slice = 10;  /* 10 ticks = 100ms quantum */

        /* Trigger context switch */
        yield();
    }
}

/**
 * Start the scheduler (first context switch)
 */
void scheduler_start(void)
{
    process_t *first;

    if (!scheduler.initialized) {
        klog_fatal("scheduler_start: Not initialized");
        while (1) {
            __asm__ volatile("wfi");
        }
    }

    if (scheduler.ready_head == NULL) {
        klog_fatal("scheduler_start: No processes to run!");
        while (1) {
            __asm__ volatile("wfi");
        }
    }

    klog_info("Starting scheduler...");

    /* Get first process from ready queue */
    first = scheduler.ready_head;
    scheduler.ready_head = first->next;
    if (scheduler.ready_head == NULL) {
        scheduler.ready_tail = NULL;
    }
    first->next = NULL;

    /* Set it as current and mark running */
    first->state = PROCESS_RUNNING;
    scheduler.current = first;
    process_set_current(first);
    scheduler.context_switches++;

    klog_info("Starting first process: %s (PID %u)", first->name, (uint32_t)first->pid);

    /* Jump to first process (never returns) */
    /* Use simple BR - SVC will set ELR_EL1 automatically when it traps */
    __asm__ volatile(
        "msr spsel, #1\n"       /* Select SP_EL1 for exception handling */
        "mov sp, %0\n"          /* Set stack pointer to process stack */
        "mov x29, %1\n"         /* Set frame pointer */
        "br %2\n"               /* Branch to entry point */
        :
        : "r"(first->sp), "r"(first->x29), "r"(first->x30)
        : "memory"
    );

    /* Should never return */
    klog_fatal("scheduler_start: Returned from first process!");
    while (1) {
        __asm__ volatile("wfi");
    }
}

/**
 * Get scheduler statistics
 */
void scheduler_get_stats(scheduler_stats_t *stats)
{
    kprintf("  scheduler_get_stats: entry\n");

    if (stats == NULL) {
        kprintf("  scheduler_get_stats: NULL stats, returning\n");
        return;
    }

    kprintf("  scheduler_get_stats: copying stats: ");
    kprintf("t");
    stats->total_processes = scheduler.total_processes;
    kprintf("r");
    stats->running_processes = scheduler.running_processes;
    kprintf("c");
    stats->context_switches = scheduler.context_switches;
    kprintf(" done\n");
}

/* ============================================================================
 * End of scheduler.c
 * ============================================================================ */
