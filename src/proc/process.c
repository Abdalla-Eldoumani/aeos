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

    kprintf("process_create: entry for '%s'\n", name ? name : "NULL");

    if (entry_point == NULL) {
        klog_error("process_create: NULL entry point");
        return NULL;
    }

    /* Allocate PCB */
    kprintf("  Allocating PCB...\n");
    proc = (process_t *)kmalloc(sizeof(process_t));
    kprintf("  PCB allocated at %p\n", proc);
    if (proc == NULL) {
        klog_error("process_create: Failed to allocate PCB");
        return NULL;
    }

    /* Allocate stack */
    kprintf("  Allocating stack...\n");
    proc->stack_base = kmalloc(PROCESS_STACK_SIZE);
    kprintf("  Stack allocated at %p\n", proc->stack_base);
    if (proc->stack_base == NULL) {
        klog_error("process_create: Failed to allocate stack");
        kfree(proc);
        return NULL;
    }

    /* Create file descriptor table */
    kprintf("  Creating file descriptor table...\n");
    proc->fd_table = vfs_fd_table_create();
    if (proc->fd_table == NULL) {
        klog_error("process_create: Failed to create fd table");
        kfree(proc->stack_base);
        kfree(proc);
        return NULL;
    }

    /* Initialize PCB */
    kprintf("  Initializing PCB fields: ");
    kprintf("p"); proc->pid = next_pid++;
    kprintf("s"); proc->state = PROCESS_READY;
    kprintf("n"); proc->name = name;
    kprintf("z"); proc->stack_size = PROCESS_STACK_SIZE;
    kprintf("t"); proc->time_slice = 0;
    kprintf("T"); proc->total_time = 0;
    kprintf("N"); proc->next = NULL;
    kprintf(" done\n");

    /* Set up initial context */
    /* Stack grows downward, so SP points to top of stack */
    kprintf("  Setting up context: ");
    stack_top = (uint64_t *)((uint64_t)proc->stack_base + PROCESS_STACK_SIZE);

    /* Align stack to 16 bytes (ARM64 requirement) */
    kprintf("S"); proc->sp = ((uint64_t)stack_top) & ~0xFULL;

    /* Set up return address to entry point */
    kprintf("L"); proc->x30 = (uint64_t)entry_point;

    /* Initialize frame pointer */
    kprintf("F"); proc->x29 = proc->sp;

    /* Clear other callee-saved registers */
    kprintf("R");
    kprintf("a"); proc->x19 = 0;
    kprintf("b"); proc->x20 = 0;
    kprintf("c"); proc->x21 = 0;
    kprintf("d"); proc->x22 = 0;
    kprintf("e"); proc->x23 = 0;
    kprintf("f"); proc->x24 = 0;
    kprintf("g"); proc->x25 = 0;
    kprintf("h"); proc->x26 = 0;
    kprintf("i"); proc->x27 = 0;
    kprintf("j"); proc->x28 = 0;
    kprintf(" done\n");

    /* Add to scheduler */
    kprintf("  Adding to scheduler...\n");
    scheduler_add_process(proc);
    kprintf("  Added to scheduler\n");

    klog_debug("Created process PID=%u '%s' at %p, stack=%p",
               (uint32_t)proc->pid, name, proc, proc->stack_base);

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
 * Get process by PID
 */
process_t *process_get_by_pid(uint64_t pid)
{
    /* TODO: Implement process table lookup */
    /* For now, just return NULL */
    (void)pid;
    return NULL;
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
