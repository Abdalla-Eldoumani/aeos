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
    uint64_t heap_bytes;            /* Bytes attributable to this PCB: its own
                                     * struct + its kernel stack + (for a full
                                     * process) its fd table. NOT a global heap
                                     * profile: the kernel heap is shared
                                     * (heap_get_stats is global-only) and kmalloc
                                     * has no owner argument, so true per-process
                                     * attribution is out of scope (the full
                                     * profiler is deferred). A DISPLAY/diagnostic
                                     * field read by cmd_ps; no invariant depends
                                     * on it. Set at create in process_create
                                     * (struct + stack + fd table) and in the
                                     * registry-only minters user_proc_register /
                                     * process_register_system (struct only - they
                                     * allocate no stack and no fd table). */

    /* Registry (SCHEDULER-INDEPENDENT). reg_next links the PCB onto a parallel
     * list (registry_head in process.c) used by ps and process_kill; it is
     * DISTINCT from the scheduler's next pointer so the registry never touches
     * the run queue. kill_requested is set by process_kill and honored at the
     * EL0 syscall boundary via the loader's current_user_proc pointer. */
    struct process *reg_next;       /* Next process in the enumeration registry */
    bool kill_requested;            /* Kill flag, honored at the next EL0 svc */
    bool killable;                  /* Only a user_proc_register'd EL0 run is
                                     * killable. process_create leaves this false
                                     * so process_kill refuses idle (PID 1) and
                                     * kernel threads (WR-03). */
    uint32_t last_cpu;              /* The cpu id this process last ran on / was
                                     * last touched on, set from smp_cpu_id()
                                     * (0 = the primary). Initialized in
                                     * process_create / user_proc_register /
                                     * process_register_system and refreshed in
                                     * process_set_current. A DISPLAY/diagnostic
                                     * field (cmd_ps reads it); no invariant
                                     * depends on its precise value, so the
                                     * cross-core write needs no lock (an aligned
                                     * uint32_t store is atomic on AArch64).
                                     * EXCEPTION: for the per-core idle markers
                                     * (idle/cpuN) last_cpu is the REPRESENTED
                                     * core (cpuN's marker = N), not the
                                     * registrar's core, so ps's CPU column shows
                                     * cores 0..3 in the bounded scope. */

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
 * Mint a REGISTRY-ONLY system marker PCB (heap PCB, fresh pid, name,
 * state=PROCESS_RUNNING, prepended on the registry, NOT enqueued so ready_head
 * stays NULL). Like user_proc_register but killable=false, so process_kill
 * refuses it - the same as idle (PID 1) and kernel threads (WR-03). Used by the
 * SMP per-core idle markers ("idle/cpuN") so they appear in ps without being
 * arm-able by `kill`. Returns the PCB, or NULL on allocation failure.
 */
process_t *process_register_system(const char *name);

/**
 * Look up a registered process by pid and set its kill_requested flag, honored
 * at the next EL0 syscall boundary (via the loader's current_user_proc pointer).
 * Returns 0 if a KILLABLE process with that pid is registered, negative
 * otherwise. A PCB is killable only if user_proc_register minted it (an EL0
 * run); idle (PID 1) and kernel threads are not killable, so `kill 1` is refused
 * rather than reported as a misleading success (WR-03). Scope B: this reaps a
 * synchronously-running EL0 program at its next svc, NOT a concurrently-running
 * one (no preemption until Phase 7).
 */
int process_kill(uint64_t pid);

#endif /* AEOS_PROCESS_H */
