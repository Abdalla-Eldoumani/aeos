# Process Management - Implementation Details

## process.c - Process Control Block Management

### Process Creation

```c
process_t *process_create(process_entry_t entry_point, const char *name)
{
    /* Allocate PCB */
    proc = (process_t *)kmalloc(sizeof(process_t));

    /* Allocate stack */
    proc->stack_base = kmalloc(PROCESS_STACK_SIZE);  /* 16KB */

    /* Create file descriptor table */
    proc->fd_table = vfs_fd_table_create();

    /* Initialize PCB fields */
    proc->pid = next_pid++;
    proc->state = PROCESS_READY;
    proc->name = name;
    proc->stack_size = PROCESS_STACK_SIZE;

    /* Set up initial context */
    stack_top = (uint64_t *)((uint64_t)proc->stack_base + PROCESS_STACK_SIZE);
    proc->sp = ((uint64_t)stack_top) & ~0xFULL;  /* 16-byte align */
    proc->x30 = (uint64_t)entry_point;           /* Return address = entry point */
    proc->x29 = proc->sp;                        /* Frame pointer = SP */

    /* Clear callee-saved registers */
    proc->x19 = proc->x20 = proc->x21 = 0;
    proc->x22 = proc->x23 = proc->x24 = 0;
    proc->x25 = proc->x26 = proc->x27 = proc->x28 = 0;

    /* Add to scheduler */
    scheduler_add_process(proc);

    return proc;
}
```

**Key Points**:
- Stack grows downward, so SP starts at top
- x30 (link register) set to entry point - when context_switch returns, it jumps there
- x29 (frame pointer) set to SP for proper stack traces

### Process Exit

```c
void process_exit(void)
{
    process_t *proc = process_current();

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
    while (1) {
        __asm__ volatile("wfi");
    }
}
```

**Memory Leak**: Stack and PCB are never freed. In a real OS, parent process would call `wait()` to clean up.

## scheduler.c - Cooperative Round-Robin Scheduler

### Scheduler State

```c
static struct {
    process_t *current;         /* Currently running process */
    process_t *idle;            /* Idle process */
    process_t *ready_head;      /* Head of ready queue */
    process_t *ready_tail;      /* Tail of ready queue */
    uint64_t total_processes;
    uint64_t running_processes;
    uint64_t context_switches;
    bool initialized;
} scheduler;
```

### Initialization

```c
void scheduler_init(void)
{
    /* Create idle process */
    process_t *idle = process_create(idle_process, "idle");

    /* Remove idle from ready queue (it's special) */
    scheduler_remove_process(idle);

    /* Store idle process reference */
    scheduler.idle = idle;

    /* Set idle as initial current process */
    scheduler.current = idle;
    idle->state = PROCESS_RUNNING;
    process_set_current(idle);

    scheduler.initialized = true;
}
```

**Why Idle is Special**: It only runs when ready queue is empty. Keeping it in the queue would waste time scheduling it unnecessarily.

### Idle Process Function

```c
static void idle_process(void)
{
    while (1) {
        __asm__ volatile("wfi");  /* Wait for interrupt (low power) */
        yield();                  /* Check for runnable processes */
    }
}
```

**WFI**: Wait For Interrupt - puts CPU in low-power state until IRQ/FIQ/etc. Since interrupts are disabled, this essentially does nothing but prevent busy-looping.

### Adding to Ready Queue

```c
void scheduler_add_process(process_t *proc)
{
    proc->next = NULL;

    if (scheduler.ready_head == NULL) {
        /* First process in queue */
        scheduler.ready_head = proc;
        scheduler.ready_tail = proc;
    } else {
        /* Add to tail */
        scheduler.ready_tail->next = proc;
        scheduler.ready_tail = proc;
    }

    proc->state = PROCESS_READY;
    scheduler.total_processes++;
    scheduler.running_processes++;
}
```

**FIFO Queue**: New processes added to tail, scheduler takes from head.

### Removing from Ready Queue

```c
void scheduler_remove_process(process_t *proc)
{
    process_t *current, *prev = NULL;

    /* Find process in queue */
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
            return;
        }
        prev = current;
    }
}
```

**Linear Search**: O(n) complexity. Could be improved with hash table or direct pointers.

### Scheduling Decision

```c
process_t *schedule(void)
{
    if (scheduler.ready_head == NULL) {
        /* No ready processes, return idle */
        return scheduler.idle;
    }

    /* Take first process from ready queue */
    process_t *next = scheduler.ready_head;

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
```

**Round-Robin**: Current process goes to back of queue, next process taken from front.

### Yield (Context Switch)

```c
void yield(void)
{
    process_t *from, *to;

    /* Get next process */
    to = schedule();

    from = scheduler.current;

    /* If switching to same process, nothing to do */
    if (from == to) {
        return;
    }

    /* Update states */
    if (from != NULL && from->state == PROCESS_RUNNING) {
        from->state = PROCESS_READY;
    }

    to->state = PROCESS_RUNNING;
    scheduler.current = to;
    process_set_current(to);

    scheduler.context_switches++;

    /* Perform actual context switch */
    if (from != NULL) {
        context_switch(from, to);
    } else {
        /* First time - just jump to new process */
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
```

**First-Time Special Case**: When `from` is NULL (first scheduling), we can't save context. Just load new context and jump.

### Scheduler Start

```c
void scheduler_start(void)
{
    /* Get first process from ready queue */
    process_t *first = scheduler.ready_head;
    scheduler.ready_head = first->next;
    if (scheduler.ready_head == NULL) {
        scheduler.ready_tail = NULL;
    }
    first->next = NULL;

    /* Set as current and mark running */
    first->state = PROCESS_RUNNING;
    scheduler.current = first;
    process_set_current(first);
    scheduler.context_switches++;

    /* Jump to first process (never returns) */
    __asm__ volatile(
        "msr spsel, #1\n"       /* Select SP_EL1 */
        "mov sp, %0\n"          /* Set stack pointer */
        "mov x29, %1\n"         /* Set frame pointer */
        "br %2\n"               /* Branch to entry point */
        :
        : "r"(first->sp), "r"(first->x29), "r"(first->x30)
        : "memory"
    );

    /* Should never return */
    while (1) {
        __asm__ volatile("wfi");
    }
}
```

**SPSel**: Ensures we're using SP_EL1 (not SP_EL0).

## context.asm - Context Switching

### Context Switch Function

```assembly
    .global context_switch
context_switch:
    /* Save current process context (from = x0) */
    stp x19, x20, [x0, #0x18]   /* Save x19, x20 at offset 0x18 */
    stp x21, x22, [x0, #0x28]
    stp x23, x24, [x0, #0x38]
    stp x25, x26, [x0, #0x48]
    stp x27, x28, [x0, #0x58]
    stp x29, x30, [x0, #0x68]   /* Save FP, LR */

    /* Save current stack pointer */
    mov x2, sp
    str x2, [x0, #0x78]

    /* Restore next process context (to = x1) */
    ldp x19, x20, [x1, #0x18]
    ldp x21, x22, [x1, #0x28]
    ldp x23, x24, [x1, #0x38]
    ldp x25, x26, [x1, #0x48]
    ldp x27, x28, [x1, #0x58]
    ldp x29, x30, [x1, #0x68]

    /* Restore next process's stack pointer */
    ldr x2, [x1, #0x78]
    mov sp, x2

    /* Return to next process */
    ret
```

**STP**: Store Pair - stores two 64-bit registers in one instruction.
**LDP**: Load Pair - loads two 64-bit registers in one instruction.

**Return Behavior**:
- First time a process runs: x30 points to entry function, `ret` jumps there
- After yield: x30 points to instruction after `context_switch` call, `ret` returns there

### Why It Works

When process A calls `yield()`:
1. `yield()` calls `context_switch(A, B)`
2. `context_switch` saves A's x30 (which points to return address in `yield()`)
3. Restores B's x30 (either entry point or return address from previous yield)
4. `ret` jumps to B's x30

When B eventually yields back to A:
1. B's x30 is saved (points to return address in B's `yield()`)
2. A's x30 is restored (points to return address in A's `yield()`)
3. `ret` returns to A's `yield()`, which then returns to A's caller

## Debugging

### Check Current Process

```c
process_t *p = process_current();
kprintf("Current: PID=%u '%s'\n", p->pid, p->name);
```

### Verify Stack Alignment

```c
uint64_t sp;
__asm__ volatile("mov %0, sp" : "=r"(sp));
assert((sp & 0xF) == 0);  /* Must be 16-byte aligned */
```

### Trace Context Switches

Add to `yield()`:
```c
klog_debug("Switch: %s -> %s", from->name, to->name);
```

### Check for Stack Overflow

```c
/* In each process, check distance from SP to stack base */
uint64_t sp;
__asm__ volatile("mov %0, sp" : "=r"(sp));
process_t *p = process_current();
size_t used = (uint64_t)p->stack_base + p->stack_size - sp;
if (used > p->stack_size - 1024) {
    kprintf("WARNING: Stack almost full (%u bytes used)\n", used);
}
```
