/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/proc/process.c
 * Description: Process management implementation
 * ============================================================================ */

#include <aeos/process.h>
#include <aeos/scheduler.h>
#include <aeos/heap.h>
#include <aeos/kprintf.h>
#include <aeos/uart.h>
#include <aeos/types.h>
#include <aeos/vfs.h>
#include <aeos/string.h>

/* Process ID counter */
static uint64_t next_pid = 1;

/* Current running process */
static process_t *current_process = NULL;

/**
 * Create a new process
 */
process_t *process_create(process_entry_t entry_point, const char *name)
{
    process_t *proc;
    uint64_t *stack_top;

    if (entry_point == NULL) {
        klog_error("process_create: NULL entry point");
        return NULL;
    }

    /* Allocate PCB */
    proc = (process_t *)kmalloc(sizeof(process_t));
    if (proc == NULL) {
        klog_error("process_create: Failed to allocate PCB");
        return NULL;
    }

    /* Allocate stack */
    proc->stack_base = kmalloc(PROCESS_STACK_SIZE);
    if (proc->stack_base == NULL) {
        klog_error("process_create: Failed to allocate stack");
        kfree(proc);
        return NULL;
    }

    /* Create file descriptor table */
    proc->fd_table = vfs_fd_table_create();
    if (proc->fd_table == NULL) {
        klog_error("process_create: Failed to create fd table");
        kfree(proc->stack_base);
        kfree(proc);
        return NULL;
    }

    /* Initialize PCB */
    proc->pid = next_pid++;
    proc->state = PROCESS_READY;

    /* Copy the caller name into the PCB so the PCB owns the storage and ps
     * never dereferences a freed or out-of-scope caller pointer. A NULL name
     * becomes "(unnamed)" rather than being passed to strncpy. */
    strncpy(proc->name, (name != NULL) ? name : "(unnamed)", PROCESS_NAME_MAX - 1);
    proc->name[PROCESS_NAME_MAX - 1] = '\0';

    proc->stack_size = PROCESS_STACK_SIZE;
    proc->time_slice = 0;
    proc->total_time = 0;
    proc->next = NULL;

    /* Set up initial context. Stack grows downward, so SP points to top */
    stack_top = (uint64_t *)((uint64_t)proc->stack_base + PROCESS_STACK_SIZE);

    /* Align stack to 16 bytes (ARM64 requirement) */
    proc->sp = ((uint64_t)stack_top) & ~0xFULL;

    /* Return address goes to entry point; frame pointer mirrors SP */
    proc->x30 = (uint64_t)entry_point;
    proc->x29 = proc->sp;

    /* Clear other callee-saved registers */
    proc->x19 = 0;
    proc->x20 = 0;
    proc->x21 = 0;
    proc->x22 = 0;
    proc->x23 = 0;
    proc->x24 = 0;
    proc->x25 = 0;
    proc->x26 = 0;
    proc->x27 = 0;
    proc->x28 = 0;

    scheduler_add_process(proc);

    klog_debug("Created process PID=%u '%s' at %p, stack=%p",
               (uint32_t)proc->pid, proc->name, proc, proc->stack_base);

    return proc;
}

/**
 * Exit current process
 */
void process_exit(void)
{
    process_t *proc = process_current();

    if (proc == NULL) {
        klog_fatal("process_exit: No current process - halting");
        while (1) {
            __asm__ volatile("wfi");
        }
    }

    klog_info("Process PID=%u '%s' exiting", (uint32_t)proc->pid, proc->name);

    /* Destroy file descriptor table (closes all open files) */
    if (proc->fd_table != NULL) {
        vfs_fd_table_destroy(proc->fd_table);
        proc->fd_table = NULL;
    }

    /* Mark as zombie */
    proc->state = PROCESS_ZOMBIE;

    /* Remove from scheduler */
    scheduler_remove_process(proc);

    /* Yield to next process (never returns) */
    yield();

    /* Should never reach here */
    klog_fatal("process_exit: yield() returned!");
    while (1) {
        __asm__ volatile("wfi");
    }
}

/**
 * Get current running process
 */
process_t *process_current(void)
{
    return current_process;
}

/**
 * Set current running process (called by scheduler)
 */
void process_set_current(process_t *proc)
{
    current_process = proc;
}

/**
 * Initialize process subsystem
 */
void process_init(void)
{
    klog_info("Initializing process subsystem...");

    /* Initialize current process to NULL */
    current_process = NULL;

    /* Scheduler will create idle process */

    klog_info("Process subsystem initialized");
}

/* ============================================================================
 * End of process.c
 * ============================================================================ */
