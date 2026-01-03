# Section 04: Process Management

## Overview

This section implements cooperative multitasking for AEOS with a round-robin scheduler. Processes are kernel threads running at EL1 with no memory protection.

## Components

### Process Control Block (process.c)
- **Location**: `src/proc/process.c`
- **Purpose**: Process creation, termination, and management
- **Features**:
  - Process creation with stack allocation
  - File descriptor table per process
  - Current process tracking
  - Process cleanup on exit

### Scheduler (scheduler.c)
- **Location**: `src/proc/scheduler.c`
- **Purpose**: Round-robin cooperative scheduling
- **Features**:
  - Ready queue management
  - Idle process
  - Cooperative context switching via yield()
  - Scheduler statistics

### Context Switching (context.asm)
- **Location**: `src/proc/context.asm`
- **Purpose**: Save and restore process context
- **Features**:
  - Callee-saved register preservation
  - ARM64 ABI compliance
  - Zero-overhead switching

## Process Model

### Kernel Threads
- All processes run at EL1 (kernel mode)
- No user space (EL0) support
- No memory protection between processes
- All code shares the same address space

### Cooperative Scheduling
- Processes voluntarily call `yield()` to give up CPU
- No preemption (timer interrupts not enabled)
- Long-running processes must yield periodically

## Process Control Block (PCB)

```c
typedef struct process {
    uint64_t pid;               /* Process ID */
    uint32_t state;             /* READY, RUNNING, ZOMBIE */
    const char *name;           /* Process name */

    /* Saved context (callee-saved registers) */
    uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
    uint64_t x29;               /* Frame pointer */
    uint64_t x30;               /* Link register */
    uint64_t sp;                /* Stack pointer */

    /* Memory */
    void *stack_base;           /* Stack allocation */
    size_t stack_size;          /* Stack size (16KB) */

    /* File descriptors */
    vfs_fd_table_t *fd_table;   /* Open files */

    /* Scheduling */
    uint64_t time_slice;        /* Unused (no preemption) */
    uint64_t total_time;        /* Unused */

    struct process *next;       /* Ready queue link */
} process_t;
```

## Process States

- **READY**: In ready queue, waiting to run
- **RUNNING**: Currently executing
- **ZOMBIE**: Terminated, awaiting cleanup

**Note**: No BLOCKED state - processes can't block on I/O (no interrupt-driven I/O).

## Scheduler Design

### Ready Queue
Singly-linked list (FIFO) of READY processes. Scheduler removes from head and adds to tail for round-robin behavior.

### Idle Process
Special process that runs when ready queue is empty. Simply loops calling `wfi` (wait for interrupt) and `yield()`.

### Context Switch
When `yield()` is called:
1. Get next process from ready queue (or idle if empty)
2. If current process is RUNNING, set to READY and add to tail of queue
3. Set next process to RUNNING
4. Call `context_switch(from, to)` in assembly

## Context Switch Implementation

### Saved Registers
Only callee-saved registers per ARM64 ABI:
- x19-x28 (10 general-purpose registers)
- x29 (frame pointer)
- x30 (link register)
- SP (stack pointer)

### Why Not All Registers?
Caller-saved registers (x0-x18) are assumed to be saved by the calling function before `yield()`. This is standard C calling convention.

### PCB Offsets
```
Offset  Field
0x00    pid
0x08    state
0x10    name
0x18    x19
0x20    x20
0x28    x21
0x30    x22
0x38    x23
0x40    x24
0x48    x25
0x50    x26
0x58    x27
0x60    x28
0x68    x29 (FP)
0x70    x30 (LR)
0x78    sp
```

## API Reference

### Process Management

```c
/* Create new process */
process_t *process_create(process_entry_t entry_point, const char *name);

/* Exit current process (never returns) */
void process_exit(void);

/* Get current process */
process_t *process_current(void);

/* Set current process (called by scheduler) */
void process_set_current(process_t *proc);
```

### Scheduler

```c
/* Initialize scheduler (creates idle process) */
void scheduler_init(void);

/* Add process to ready queue */
void scheduler_add_process(process_t *proc);

/* Remove process from scheduler */
void scheduler_remove_process(process_t *proc);

/* Yield CPU to next process */
void yield(void);

/* Start scheduler (first context switch, never returns) */
void scheduler_start(void);

/* Get scheduler statistics */
void scheduler_get_stats(scheduler_stats_t *stats);
```

## Usage Examples

### Creating a Process

```c
void my_process(void) {
    while (1) {
        kprintf("Process running\n");
        yield();  /* Give CPU to others */
    }
}

int main() {
    process_init();
    scheduler_init();

    process_create(my_process, "my_process");

    scheduler_start();  /* Never returns */
}
```

### Cooperative Yielding

```c
void worker_process(void) {
    for (int i = 0; i < 1000; i++) {
        /* Do work */
        if (i % 100 == 0) {
            yield();  /* Yield every 100 iterations */
        }
    }
    sys_exit(0);
}
```

## Important Notes

### Stack Size
Each process gets 16KB stack (PROCESS_STACK_SIZE). Deep recursion or large stack allocations can overflow.

### Stack Alignment
ARM64 requires 16-byte stack alignment. The boot code ensures initial stack is aligned, and each process stack is also aligned.

### First Context Switch
`scheduler_start()` performs the first context switch differently than normal `yield()`:
- Sets up SP directly from first process
- Branches to entry point (doesn't restore context)
- Never returns

### Process Cleanup
When a process calls `sys_exit()`:
1. File descriptor table is destroyed (closes all open files)
2. State set to ZOMBIE
3. Process removed from scheduler
4. `yield()` called (never returns)
5. Stack is NOT freed (memory leak - no process cleanup yet)

### Idle Process Stack
The idle process is created during `scheduler_init()`. It has its own stack and appears in the ready queue initially, then is removed and stored separately.

## Known Issues

### No Stack Cleanup
When a process exits, its stack memory is never freed. This is a known memory leak.

**Workaround**: Don't create/destroy processes frequently. Most AEOS processes run forever.

### No Process Termination
There's no mechanism to clean up zombie processes. They remain in memory forever.

### Single-Threaded Initialization
The current implementation assumes single-threaded execution during initialization. Race conditions could occur if multiple CPUs were active.

### Verbose Debug Output
Process creation prints extensive debug output (lettered progress indicators). This was left in for debugging stuck situations.

## Testing

### Basic Scheduling Test

```c
void process_a(void) {
    for (int i = 0; i < 5; i++) {
        kprintf("A ");
        yield();
    }
}

void process_b(void) {
    for (int i = 0; i < 5; i++) {
        kprintf("B ");
        yield();
    }
}

/* Expected output: A B A B A B A B A B (round-robin) */
```

### Stack Usage Test

```c
void deep_recursion(int depth) {
    char buffer[1024];  /* Use stack */
    if (depth > 0) {
        deep_recursion(depth - 1);
    }
}

/* Test: deep_recursion(10) should work, deep_recursion(20) might overflow */
```

### Scheduler Stats

```c
scheduler_stats_t stats;
scheduler_get_stats(&stats);

kprintf("Total processes: %u\n", stats.total_processes);
kprintf("Running: %u\n", stats.running_processes);
kprintf("Context switches: %llu\n", stats.context_switches);
```

## Performance

### Context Switch Overhead
Minimal - only 13 registers saved/restored. On ARM64 Cortex-A57, this is approximately 30-50 CPU cycles.

### Scheduling Decision
O(1) - always takes head of ready queue.

### Ready Queue Operations
- Add: O(1) - append to tail
- Remove: O(n) - must search list (used for removal, not normal scheduling)

## Future Enhancements

- Preemptive scheduling (requires timer interrupts)
- Priority-based scheduling
- Sleep/wake mechanisms
- Proper process termination and cleanup
- Process accounting (CPU time tracking)
- Multi-level feedback queue