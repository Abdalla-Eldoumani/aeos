/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/process.h
 * Description: Process management interface
 * ============================================================================ */

#ifndef AEOS_PROCESS_H
#define AEOS_PROCESS_H

#include <aeos/types.h>

/* Forward declaration for VFS file descriptor table */
struct vfs_fd_table;

/* Process stack size (4KB per process) */
#define PROCESS_STACK_SIZE  4096

/**
 * Process states
 */
typedef enum {
    PROCESS_READY,      /* Ready to run */
    PROCESS_RUNNING,    /* Currently executing */
    PROCESS_BLOCKED,    /* Waiting for resource */
    PROCESS_ZOMBIE      /* Exited, awaiting cleanup */
} process_state_t;

/**
 * Process Control Block (PCB)
 * Contains all information needed to manage a process
 */
typedef struct process {
    /* Process identification */
    uint64_t pid;                   /* Process ID */
    process_state_t state;          /* Current state */
    const char *name;               /* Process name (for debugging) */

    /* CPU context (callee-saved registers per ARM64 ABI) */
    uint64_t x19, x20, x21, x22;    /* Callee-saved GPRs */
    uint64_t x23, x24, x25, x26;
    uint64_t x27, x28, x29;         /* x29 = Frame Pointer */
    uint64_t x30;                   /* Link Register (return address) */
    uint64_t sp;                    /* Stack Pointer */

    /* Memory management */
    void *stack_base;               /* Base of stack allocation */
    size_t stack_size;              /* Stack size (bytes) */

    /* File system */
    struct vfs_fd_table *fd_table;  /* File descriptor table */

    /* Scheduling */
    struct process *next;           /* Next process in scheduler queue */
    uint64_t time_slice;            /* Time quantum (for preemptive scheduling) */
    uint64_t total_time;            /* Total CPU time used */

} process_t;

/**
 * Process entry point function type
 */
typedef void (*process_entry_t)(void);

/**
 * Create a new process
 *
 * @param entry_point Function to execute
 * @param name Process name (for debugging)
 * @return Pointer to new PCB, or NULL on failure
 */
process_t *process_create(process_entry_t entry_point, const char *name);

/**
 * Exit current process
 * Marks process as ZOMBIE and yields to scheduler
 */
void process_exit(void) __attribute__((noreturn));

/**
 * Get current running process
 *
 * @return Pointer to current process PCB
 */
process_t *process_current(void);

/**
 * Get process by PID
 *
 * @param pid Process ID
 * @return Pointer to PCB, or NULL if not found
 */
process_t *process_get_by_pid(uint64_t pid);

/**
 * Initialize process subsystem
 * Creates the kernel idle process
 */
void process_init(void);

/**
 * Set current running process (internal - called by scheduler)
 *
 * @param proc Process to set as current
 */
void process_set_current(process_t *proc);

#endif /* AEOS_PROCESS_H */
