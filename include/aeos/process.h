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

/* Maximum length of a process name including the NUL terminator. The PCB owns
 * a fixed copy of the name, so a caller may pass a transient string. */
#define PROCESS_NAME_MAX    32

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
    char name[PROCESS_NAME_MAX];    /* Process name (for debugging) */

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

    /* Registry (SCHEDULER-INDEPENDENT). reg_next links the PCB onto a parallel
     * list (registry_head in process.c) used by ps and process_kill; it is
     * DISTINCT from the scheduler's next pointer so the registry never touches
     * the run queue. kill_requested is set by process_kill and honored at the
     * EL0 syscall boundary via the loader's current_user_proc pointer. */
    struct process *reg_next;       /* Next process in the enumeration registry */
    bool kill_requested;            /* Kill flag, honored at the next EL0 svc */

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

/**
 * Link/unlink a PCB on the SCHEDULER-INDEPENDENT enumeration registry (reg_next +
 * registry_head), distinct from the scheduler run queue. process_create calls
 * process_register so every created process shows in ps; process_exit calls
 * process_unregister so a reaped process leaves the registry.
 */
void process_register(process_t *proc);
void process_unregister(process_t *proc);

/**
 * Head of the registry list; walk it via proc->reg_next (NULL-terminated) to
 * enumerate live processes (pid/name/state) without touching the scheduler.
 */
process_t *process_registry_head(void);

/**
 * Mint a REGISTRY-ONLY PCB for a synchronous EL0 run: a heap PCB with pid,
 * name, state=PROCESS_RUNNING, kill_requested=false, prepended on the registry.
 * It is NEVER enqueued in the scheduler (no scheduler_add_process, no kernel
 * stack), so ready_head stays NULL and the dormant scheduler is not woken - the
 * Scope B boot-stability invariant. The caller frees it (process_unregister +
 * kfree) after the run. Returns the PCB, or NULL on allocation failure.
 */
process_t *user_proc_register(const char *name);

/**
 * Look up a registered process by pid and set its kill_requested flag, honored
 * at the next EL0 syscall boundary (via the loader's current_user_proc pointer).
 * Returns 0 if a process with that pid is registered, negative otherwise. Scope
 * B: this reaps a synchronously-running EL0 program at its next svc, NOT a
 * concurrently-running one (no preemption until Phase 7).
 */
int process_kill(uint64_t pid);

#endif /* AEOS_PROCESS_H */
