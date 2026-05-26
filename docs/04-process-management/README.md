# Section 04: Process Management

## Overview

This section implements preemptive multitasking for AEOS with a round-robin scheduler. Processes are kernel threads running at EL1 with no memory protection. The 100 Hz timer tick enables automatic context switching.

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
- **Purpose**: Round-robin preemptive scheduling
- **Features**:
  - Ready queue management
  - Idle process
  - Preemptive context switching via timer tick (100 Hz)
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
- Scheduled processes run at EL1 (kernel mode); the scheduler itself only ever runs EL1 kernel threads
- No memory protection between EL1 kernel threads; they share one address space

### Loaded EL0 Programs
A loaded ELF runs at EL0, synchronously, one at a time. `elf_exec_file` reads a static ELF off the VFS, maps its segments into the EL0 user window (executable segments read-only for per-segment W^X, see Section 02), maps an EL0 stack, and enters the program at EL0. The program reaches the kernel only through a trapped `svc`, and a privileged instruction from EL0 faults to EL1.

While it runs, the program holds a registry PCB: it shows up in `ps`, and `kill <pid>` arms a flag that the next `svc` honors, reaping the run. The first EL0 path was the Phase 5 one-shot in-kernel payload that proved the privilege boundary; the loaded-from-a-file program is the Phase 6 ELF loader built on the same `usermode_enter` mechanism.

The honest scope: the loaded program runs on the current EL1 stack and is never enqueued in the scheduler, so the dormant scheduler stays asleep. `kill` reaps a registered process at its next `svc`, which the headless test suite proves; interactive "kill from a second prompt while the program runs mid-flight" is not delivered, because the program runs to completion before the prompt returns. Cross-core scheduling stays out of scope (see the SMP section).

### Preemptive Scheduling
- Timer tick at 100 Hz triggers `scheduler_tick()`
- `scheduler_tick()` calls `yield()` to preempt current process
- Processes can also voluntarily call `yield()` to give up CPU
- No explicit time slice management needed (tick-based preemption)

## Process Control Block (PCB)

```c
typedef struct process {
    uint64_t pid;               /* Process ID */
    uint32_t state;             /* READY, RUNNING, ZOMBIE */
    char name[PROCESS_NAME_MAX]; /* Process name (copied, N = 32) */

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
    uint64_t time_slice;        /* Time quantum (preemptive scheduling) */
    uint64_t total_time;        /* Real CPU ticks (ps TICKS column) */
    uint64_t heap_bytes;        /* Bytes attributable to this PCB (ps HEAP_B) */

    struct process *next;       /* Ready queue link (scheduler) */

    /* Enumeration registry (scheduler-independent) */
    struct process *reg_next;   /* Registry link, distinct from next */
    bool kill_requested;        /* Set by process_kill, honored at the next EL0 svc */
    bool killable;              /* Only a loaded EL0 run is killable */
    uint32_t last_cpu;          /* Core last touched on (ps CPU column) */
} process_t;
```

`reg_next`, `kill_requested`, and `killable` are the registry fields that back `ps` and `kill` (see "Enumeration registry and kill" below). `last_cpu` is the CPU column described in the SMP section. The registry list threads through `reg_next`, distinct from the scheduler's `next`, so enumeration never touches the run queue.

### Name Ownership

The PCB owns its name. `process_create` copies the caller's `name` into the fixed `char name[PROCESS_NAME_MAX]` buffer (N = 32) with `strncpy` plus an explicit terminating NUL, rather than storing the caller's `const char *`. A caller may therefore pass a transient string; a name of 31 characters or more is truncated to fit, and a NULL name is stored as `(unnamed)`. Because the bytes live in the PCB, `ps` reads PCB-owned memory and never dereferences a freed or out-of-scope caller pointer. This closed the SEC-07 audit item.

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

/* Yield CPU to next process (cooperative) */
void yield(void);

/* Called from timer interrupt for preemption */
void scheduler_tick(void);

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

### Voluntary Yielding (Optional)

```c
void worker_process(void) {
    for (int i = 0; i < 1000; i++) {
        /* Do work */
        /* Note: yield() is optional - timer preempts automatically */
        if (i % 100 == 0) {
            yield();  /* Can yield early if work is I/O bound */
        }
    }
    sys_exit(0);
}
```

With preemptive scheduling, processes are automatically preempted every 10ms (100 Hz timer). Explicit `yield()` calls are only needed if a process wants to give up CPU earlier (e.g., waiting for I/O).

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

## Enumeration registry and kill

`ps` and `kill` need to enumerate and look up processes, but the scheduler exposes only the run queue and `current_process`. The registry is a scheduler-independent parallel list: the `reg_next` field threads each PCB through a file-static `registry_head`, distinct from the scheduler's `next`, so the registry never touches the run queue.

- `process_create` registers every process it creates, so idle and every kernel thread show in `ps`; `process_exit` unregisters a reaped process.
- A loaded EL0 program is minted a registry-only PCB (a fresh pid, `RUNNING`, `killable = true`) that is never enqueued in the scheduler, so `ready_head` stays NULL and the dormant scheduler is not woken. The loader frees it after the run.
- `process_kill(pid)` walks the registry, sets `kill_requested` on the matching PCB, and returns 0. It refuses a non-killable PCB: only a loaded EL0 run sets `killable = true`, so `kill 1` (idle) and the kernel threads are refused rather than reported as a misleading success.

The kill flag is honored at the EL0 syscall boundary, against the loaded program's PCB rather than `process_current()` (which is idle throughout the synchronous one-shot). A kill-armed run is reaped at its next `svc`, before that syscall dispatches.

## SMP bringup (Phase 7)

The QEMU virt board boots with four cortex-a57 cores. The primary core (0) runs `kernel_main`; the other three start powered off. `smp_init` brings them online:

- Each secondary is started via PSCI `CPU_ON` (an `HVC` call - QEMU virt has no EL3, so PSCI is reached at EL2 from EL1). It lands at a per-core entry that drops EL2 -> EL1 (mirroring the primary's boot drop, including resetting the virtual timer offset), sets its own stack, enables the MMU against the page tables the primary already built (the tables are shared, not rebuilt), programs its own GIC CPU interface (the shared distributor is left alone, owned by the primary), installs the shared exception vector table in its `VBAR_EL1`, prints `smp: core N online`, signals that it is up, and then parks in a `wfe` idle loop.
- The primary's wait for each secondary is **bounded**: it spins on the secondary's online flag for a fixed maximum, then moves on. A `CPU_ON` that fails, or a secondary that never reports, is logged and skipped. The primary always continues to the window manager - a stuck secondary can never hang the kernel.
- After the bringup, the serial shows `smp: core 1 online`, `smp: core 2 online`, `smp: core 3 online`, and a `smp: N cores online` summary (the total, counting the primary).

**`ps` and the per-core idle process.** For each online core, the primary registers a per-core idle process on the enumeration registry (`idle/cpu1`, `idle/cpu2`, `idle/cpu3`; the primary's own idle is PID 1). These are registry markers only - they are not enqueued in the scheduler, so the dormant scheduler stays asleep. They are registered by the primary because the heap allocator is not thread-safe; the secondaries never allocate. `ps` therefore lists a process for cores 0..3.

**The `ps` columns.** Each PCB carries a `last_cpu` field - the core a process last ran on, or was last touched on. `process_create` and the registry-mint helpers set it to the core that creates the PCB, and `process_set_current` refreshes it whenever a process becomes current, so it records the core that last ran the process. The per-core idle markers are the exception: each `idle/cpuN` marker carries its own represented core (`idle/cpu2` reads CPU 2), not the primary that registered it, so the column spans cores 0..3. Alongside the CPU column, `ps` prints a TICKS column (the real `total_time`) and a HEAP_B column (the attributed `heap_bytes`):

```
  PID  CPU  STATE      TICKS    HEAP_B   NAME
  1    0    RUNNING    4213     4944     idle
  5    1    RUNNING    0        336      idle/cpu1
  6    2    RUNNING    0        336      idle/cpu2
  7    3    RUNNING    0        336      idle/cpu3
```

This is honest about the bounded scope:

- **CPU (`last_cpu`)** shows the LAST core to touch a PCB, not live cross-core scheduling. On the production path everything runs on the primary (core 0) and the secondaries park, so the registered processes read core 0; the per-core idle markers read 1..3 because they stand in for the parked cores. The column does **not** mean a process migrates between cores - that is full cross-core scheduling, which is out of scope. `last_cpu` is a display field: an aligned word write, with no invariant depending on its value, so it needs no lock.
- **TICKS (`total_time`)** is the real CPU-tick count `scheduler_tick` increments on every 100 Hz timer tick. It is not a placeholder - it accrues continuously. In the bounded scope the scheduler is dormant (the ready queue stays empty) and idle (PID 1) is current throughout, so the ticks accrue on idle. That is honest: idle IS the running process most of the time, so its TICKS climbs while the registry markers sit at 0. It does **not** imply per-process time-sharing across the listed processes.
- **HEAP_B (`heap_bytes`)** is the heap bytes ATTRIBUTABLE to the PCB - its own `process_t` struct, its 4 KB kernel stack, and (for a full `process_create`d process) its fd table. The registry-only markers (`idle/cpuN` and a loaded EL0 run) allocate no stack and no fd table, so their HEAP_B is just the `process_t` struct. This is **not** a global heap profile: the kernel heap is shared and `kmalloc` takes no owner argument, so true per-allocation per-process attribution would mean threading an owner through every allocation - explicitly out of scope. HEAP_B is the credible per-PCB counter, the same honest "display field, documented caveat" discipline as `last_cpu`.

**The runqueue lock.** The scheduler runqueue (`ready_head`/`ready_tail`) is guarded by a spinlock so two cores cannot interleave a list operation and corrupt it. The three mutators that touch the queue - `scheduler_add_process`, `scheduler_remove_process`, and `schedule` - take the lock at entry and release it at every return path. `schedule` rotates the running process by removing and re-adding it, so it would re-acquire its own held lock and deadlock if the mutators were naively locked; instead the actual list work lives in unlocked inner helpers, and the public entry points wrap them by taking the lock exactly once. The lock is uncontended on the single-core cooperative path (the cooperative `yield` and the boot `scheduler_init`), where it costs one acquire/release, so the boot is unchanged. Its correctness under genuine contention is proven by a cross-core stress test that brings up real secondaries to hammer the mutators concurrently while a shared lock-protected counter checks for lost updates - without the lock the concurrent mutation corrupts the list and loses counter updates; with it the queue stays well-formed and the counter is exact.

**Honest scope.** The secondaries come online and report, but they do **not** run scheduled work on the production path - the scheduler stays cooperative and single-core, and the production runqueue stays empty (`ready_head` is NULL, the scheduler is dormant). Cross-core preemptive scheduling (a process actually migrating to or running on a secondary) is out of scope; the runqueue lock makes the queue safe for concurrent mutation and the cross-core stress proves that, but no scheduled work runs across cores in production.

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

- Priority-based scheduling
- Sleep/wake mechanisms
- Proper process termination and cleanup
- Per-allocation heap ownership (HEAP_B is per-PCB attribution today, not a global profiler)
- Multi-level feedback queue