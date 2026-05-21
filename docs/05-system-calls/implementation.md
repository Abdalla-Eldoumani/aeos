# System Calls - Implementation Details

## syscall.c - System Call Implementation

### Syscall Table Structure

```c
typedef uint64_t (*syscall_fn_t)(uint64_t, uint64_t, uint64_t,
                                  uint64_t, uint64_t, uint64_t);

static const syscall_fn_t syscall_table[MAX_SYSCALLS] = {
    [SYS_EXIT]   = sys_exit_impl,
    [SYS_WRITE]  = sys_write_impl,
    [SYS_READ]   = sys_read_impl,
    [SYS_GETPID] = sys_getpid_impl,
    [SYS_YIELD]  = sys_yield_impl,
};
```

**Designated Initializers**: Array indexed by syscall number. Unimplemented syscalls are NULL.

**Function Signature**: All handlers have same signature (6 uint64_t args) for uniform dispatch.

### Syscall Handler (Direct Dispatch)

```c
uint64_t syscall_handler(uint64_t syscall_num,
                         uint64_t arg0, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    /* Validate syscall number */
    if (syscall_num >= MAX_SYSCALLS) {
        return (uint64_t)-1;
    }

    /* Check if implemented */
    if (syscall_table[syscall_num] == NULL) {
        return (uint64_t)-1;
    }

    /* Update statistics */
    syscall_stats.total_syscalls++;
    syscall_stats.syscall_counts[syscall_num]++;

    /* Dispatch to implementation */
    return syscall_table[syscall_num](arg0, arg1, arg2, arg3, arg4, arg5);
}
```

**Called From**: Kernel code directly, with the syscall number passed as the first argument. There is no SVC trap and no exception-vector entry on this path. Everything runs at EL1, so the handler is reached as a plain C call.

**Arguments**: Passed straight through as C function arguments. The dispatcher does not read a saved register frame because nothing trapped to get here. A bad number (out of range or an unimplemented slot whose table entry is NULL) returns -1.

### Kernel-Side Wrappers

```c
uint64_t sys_write(int fd, const void *buf, size_t count)
{
    return sys_write_impl((uint64_t)fd, (uint64_t)buf, (uint64_t)count, 0, 0, 0);
}
```

**Purpose**: Provide a typed interface for kernel callers. The wrapper calls the implementation directly; there was never an SVC step to skip.

**Type Safety**: Converts typed args to uint64_t, passes unused args as 0.

## Individual Syscall Implementations

### sys_exit_impl

```c
static uint64_t sys_exit_impl(uint64_t arg0, uint64_t arg1, uint64_t arg2,
                               uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    int status = (int)arg0;
    process_t *proc = process_current();

    /* Unused parameters */
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;

    klog_info("sys_exit: PID %u exiting with status %d",
              (uint32_t)proc->pid, status);

    /* Call process_exit (never returns) */
    process_exit();

    return 0;  /* Never reached */
}
```

**Exit Status**: Stored but not currently used (no parent process to retrieve it).

**Never Returns**: `process_exit()` calls `yield()` after removing process from scheduler.

### sys_write_impl

```c
static uint64_t sys_write_impl(uint64_t arg0, uint64_t arg1, uint64_t arg2,
                                uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    int fd = (int)arg0;
    const void *buf = (const void *)arg1;
    size_t count = (size_t)arg2;
    const char *str = (const char *)buf;

    (void)arg3; (void)arg4; (void)arg5;

    /* Validate arguments */
    if (buf == NULL) {
        klog_error("sys_write: NULL buffer");
        return (uint64_t)-1;
    }

    /* Only stdout/stderr allowed */
    if (fd != STDOUT_FILENO && fd != STDERR_FILENO) {
        klog_error("sys_write: Invalid fd %d", fd);
        return (uint64_t)-1;
    }

    /* Write each character to UART */
    for (size_t i = 0; i < count; i++) {
        uart_putc(str[i]);
    }

    return count;  /* Return number of bytes written */
}
```

**Direct UART**: Bypasses VFS - writes directly to console. Future versions should use VFS.

**Byte-by-Byte**: Calls `uart_putc()` for each character. Slow but simple.

### sys_read_impl

```c
static uint64_t sys_read_impl(uint64_t arg0, uint64_t arg1, uint64_t arg2,
                               uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    (void)arg0; (void)arg1; (void)arg2;
    (void)arg3; (void)arg4; (void)arg5;

    klog_warn("sys_read: Not implemented yet");
    return 0;  /* EOF */
}
```

**Stub**: Always returns 0 (EOF). Console input handled separately in shell.

### sys_getpid_impl

```c
static uint64_t sys_getpid_impl(uint64_t arg0, uint64_t arg1, uint64_t arg2,
                                 uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    process_t *proc = process_current();

    (void)arg0; (void)arg1; (void)arg2;
    (void)arg3; (void)arg4; (void)arg5;

    return proc->pid;
}
```

**Trivial**: Just returns PID from current process.

### sys_yield_impl

```c
static uint64_t sys_yield_impl(uint64_t arg0, uint64_t arg1, uint64_t arg2,
                                uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    (void)arg0; (void)arg1; (void)arg2;
    (void)arg3; (void)arg4; (void)arg5;

    /* Call scheduler's yield function */
    yield();
    return 0;
}
```

**Cooperative**: Voluntarily gives up CPU to next process.

## SVC Instruction (Future Use)

This section sketches what an EL0 syscall path would look like. None of it is wired up today; the live dispatcher is the direct call above. It is here to show what changes when userspace lands.

### User-Space Invocation (future)

```assembly
/* Setup syscall number and arguments */
mov x8, #SYS_WRITE      /* Syscall number */
mov x0, #1              /* fd = STDOUT */
ldr x1, =message        /* buf = message */
mov x2, #14             /* count = 14 */
svc #0                  /* Trigger SVC exception */
/* Return value in x0 */
```

**SVC Immediate**: The `#0` is ignored on ARMv8. The syscall number would come from x8.

### Exception Vector Handling (future)

A future EL0 path would add SVC decoding to the synchronous vector in `vectors.asm`: recognize the SVC class in ESR_EL1, pull the syscall number and arguments from the saved register frame, call `syscall_handler`, and store the return value back into the saved x0 before `eret`. The current synchronous vector decodes the exception class for diagnostics only and does not run any of this; the kernel reaches `syscall_handler` by a direct call instead.

## Error Handling

### Return Value Convention

```c
/* Success: return actual result */
return count;

/* Error: return -1 cast to uint64_t */
return (uint64_t)-1;  /* 0xFFFFFFFFFFFFFFFF */
```

**Check in C**:
```c
uint64_t ret = sys_write(fd, buf, len);
if (ret == (uint64_t)-1) {
    /* Error */
}
```

**Alternative**: Some syscalls return 0 on error (like allocation failures).

### No errno

Unlike POSIX, AEOS doesn't set a global `errno`. Error information is lost.

**Future**: Could add per-process errno in PCB.

## Statistics Tracking

```c
static struct {
    uint64_t total_syscalls;
    uint64_t syscall_counts[MAX_SYSCALLS];
} syscall_stats;
```

Updated on each syscall:
```c
syscall_stats.total_syscalls++;
syscall_stats.syscall_counts[syscall_num]++;
```

**Usage**:
```c
kprintf("Total syscalls: %llu\n", syscall_stats.total_syscalls);
kprintf("sys_write calls: %llu\n", syscall_stats.syscall_counts[SYS_WRITE]);
```

## Debugging

### Trace Syscalls

Add to `syscall_handler()`:
```c
kprintf("[syscall] %u(%llx,%llx,%llx,...)\n", syscall_num, arg0, arg1, arg2);
```

### Verify Table Entries

```c
for (int i = 0; i < MAX_SYSCALLS; i++) {
    if (syscall_table[i] != NULL) {
        kprintf("Syscall %d: implemented\n", i);
    }
}
```

### Exercise the Dispatcher

```c
/* Direct numeric dispatch, the same path the kernel uses */
uint64_t pid = syscall_handler(SYS_GETPID, 0, 0, 0, 0, 0, 0);
kprintf("PID from dispatcher: %llu\n", pid);
```

An `svc #0` would not reach the dispatcher today. With no EL0 path wired up, it traps into `handle_exception`, which masks DAIF and halts. Call `syscall_handler` (or the typed wrapper) directly instead.

## Performance Considerations

### Direct Call vs SVC

**Direct Call**: ~5-10 cycles (function call overhead)
**SVC**: ~100-200 cycles (exception entry/exit, context save/restore)

For kernel threads running at EL1, direct calls are much faster.

### Syscall Table Lookup

O(1) array indexing - very fast.

### Argument Marshalling

Minimal overhead - just casting between types.