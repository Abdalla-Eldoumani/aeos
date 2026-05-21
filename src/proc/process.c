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

/* Head of the SCHEDULER-INDEPENDENT enumeration registry. This is a parallel
 * list threaded through process_t.reg_next, DISTINCT from the scheduler's run
 * queue (next/ready_head). ps walks it and process_kill looks up by pid; it
 * never touches the run queue, so user_proc_register can mint a registry-only
 * PCB without waking the dormant scheduler. */
static process_t *registry_head = NULL;

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
    proc->reg_next = NULL;
    proc->kill_requested = false;

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

    /* Auto-register on the enumeration registry so idle and kernel threads are
     * visible in ps. This is additive: the scheduler queue above is unchanged. */
    process_register(proc);

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

    /* Leave the enumeration registry so ps no longer lists the reaped process. */
    process_unregister(proc);

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
 * Prepend a PCB on the enumeration registry (reg_next). The scheduler run queue
 * (next/ready_head) is untouched.
 */
void process_register(process_t *proc)
{
    if (proc == NULL) {
        return;
    }
    proc->reg_next = registry_head;
    registry_head = proc;
}

/**
 * Unlink a PCB from the enumeration registry by walking reg_next. Safe if the
 * PCB is not on the list (no-op).
 */
void process_unregister(process_t *proc)
{
    process_t **link = &registry_head;

    if (proc == NULL) {
        return;
    }
    while (*link != NULL) {
        if (*link == proc) {
            *link = proc->reg_next;
            proc->reg_next = NULL;
            return;
        }
        link = &(*link)->reg_next;
    }
}

/**
 * Head of the registry list (walk via reg_next).
 */
process_t *process_registry_head(void)
{
    return registry_head;
}

/**
 * Mint a registry-only PCB for a synchronous EL0 run. It is NEVER enqueued in
 * the scheduler: no scheduler_add_process and no kernel stack, so ready_head
 * stays NULL and the dormant scheduler is not woken (Scope B). The synchronous
 * EL0 run uses the current SP_EL1, so no per-process kernel stack is needed.
 * The caller frees the PCB (process_unregister + kfree) after the run.
 */
process_t *user_proc_register(const char *name)
{
    process_t *proc = (process_t *)kmalloc(sizeof(process_t));
    if (proc == NULL) {
        klog_error("user_proc_register: failed to allocate PCB");
        return NULL;
    }

    /* Zero scalar PCB fields. memset is safe here (a heap PCB, no vector data);
     * the fields the registry/kill path reads are then set explicitly below. */
    memset(proc, 0, sizeof(process_t));
    proc->pid = next_pid++;
    strncpy(proc->name, (name != NULL) ? name : "(unnamed)", PROCESS_NAME_MAX - 1);
    proc->name[PROCESS_NAME_MAX - 1] = '\0';
    proc->state = PROCESS_RUNNING;
    proc->kill_requested = false;
    proc->next = NULL;

    /* Registry-only: prepend on the parallel list, do NOT scheduler_add_process. */
    process_register(proc);

    return proc;
}

/**
 * Set the kill flag on a registered process by pid. Honored at the next EL0
 * syscall boundary via the loader's current_user_proc pointer.
 */
int process_kill(uint64_t pid)
{
    process_t *p = registry_head;

    while (p != NULL) {
        if (p->pid == pid) {
            p->kill_requested = true;
            return 0;
        }
        p = p->reg_next;
    }

    klog_warn("kill: no process with pid %u", (uint32_t)pid);
    return -1;
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
