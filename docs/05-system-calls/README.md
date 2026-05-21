# Section 05: System Calls

## Overview

This section implements the system call interface for AEOS. It uses direct function calls, not SVC instructions, because all code runs at EL1 (kernel mode) and there is no privilege boundary to cross.

## Implementation Approach

### Direct Function Calls
A syscall is a table lookup followed by a direct C call. `syscall(num, args...)` validates `num` against the table size, fetches `syscall_table[num]`, and calls the handler as an ordinary function. The kernel-side wrappers (`sys_write`, `sys_getpid`, and so on) call the implementations directly:
```c
sys_write(STDOUT_FILENO, "Hello\n", 6);
```

There is no SVC trap on the live path. The exception vectors decode the SVC class for diagnostics, but no syscall flows through them today.

### Future: SVC for EL0
If userspace at EL0 is added later, the dispatcher would move into the synchronous exception handler in `src/interrupts/exceptions.c` and pull arguments from the saved register frame:
```assembly
mov x8, #SYS_WRITE
mov x0, #1          /* STDOUT_FILENO */
ldr x1, =message
mov x2, #6
svc #0
```

That is future work, not the current mechanism.

## Components

### System Call Dispatcher (syscall.c)
- **Location**: `src/syscall/syscall.c`
- **Purpose**: Syscall routing and implementation
- **Features**:
  - Syscall table for dispatch
  - Parameter validation
  - Statistics tracking
  - Error handling

## Implemented System Calls

| Number | Name | Signature | Description |
|--------|------|-----------|-------------|
| 0 | SYS_EXIT | exit(int status) | Terminate process |
| 1 | SYS_WRITE | write(int fd, const void *buf, size_t count) | Write to file descriptor |
| 2 | SYS_READ | read(int fd, void *buf, size_t count) | Read from file descriptor (stub) |
| 3 | SYS_GETPID | getpid(void) | Get process ID |
| 4 | SYS_YIELD | yield(void) | Yield CPU to scheduler |

**Note**: SYS_READ returns 0 (EOF) - not implemented.

## System Call Convention

### Arguments
Per ARM64 calling convention:
- Syscall number: x8
- Arguments: x0-x5 (up to 6 arguments)
- Return value: x0

### Error Handling
System calls return -1 (cast to uint64_t) on error. Some return 0 on error (like allocation failures).

## API Reference

### Kernel-Side Wrappers

```c
/* Exit current process */
uint64_t sys_exit(int status);

/* Write to file descriptor */
uint64_t sys_write(int fd, const void *buf, size_t count);

/* Read from file descriptor (stub) */
uint64_t sys_read(int fd, void *buf, size_t count);

/* Get process ID */
uint64_t sys_getpid(void);

/* Yield CPU */
uint64_t sys_yield(void);
```

### Syscall Handler

```c
/* Numeric dispatch entry: validates the number, looks up the table, calls it */
uint64_t syscall_handler(uint64_t syscall_num,
                         uint64_t arg0, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5);
```

`syscall_handler` is the numeric dispatch entry. It is called directly with a syscall number, not from an SVC trap. It validates the number, rejects an out-of-range or unimplemented number with -1, and otherwise calls `syscall_table[num]`.

## Syscall Implementations

### sys_exit
```c
static uint64_t sys_exit_impl(uint64_t arg0, ...)
{
    int status = (int)arg0;
    process_t *proc = process_current();

    klog_info("sys_exit: PID %u exiting with status %d",
              (uint32_t)proc->pid, status);

    process_exit();  /* Never returns */
    return 0;
}
```

Terminates the calling process with given exit status.

### sys_write
```c
static uint64_t sys_write_impl(uint64_t arg0, uint64_t arg1, uint64_t arg2, ...)
{
    int fd = (int)arg0;
    const void *buf = (const void *)arg1;
    size_t count = (size_t)arg2;

    /* Validate arguments */
    if (buf == NULL) {
        return (uint64_t)-1;
    }

    /* Only stdout/stderr supported */
    if (fd != STDOUT_FILENO && fd != STDERR_FILENO) {
        return (uint64_t)-1;
    }

    /* Write to UART */
    for (size_t i = 0; i < count; i++) {
        uart_putc(((const char *)buf)[i]);
    }

    return count;
}
```

Currently writes to UART regardless of file descriptor (no VFS integration yet).

### sys_getpid
```c
static uint64_t sys_getpid_impl(...)
{
    process_t *proc = process_current();
    return proc->pid;
}
```

Returns current process ID.

### sys_yield
```c
static uint64_t sys_yield_impl(...)
{
    yield();  /* Call scheduler */
    return 0;
}
```

Cooperative context switch.

## Usage Examples

### Writing to Console
```c
const char *msg = "Hello, World!\n";
sys_write(STDOUT_FILENO, msg, strlen(msg));
```

### Process Exit
```c
if (error) {
    sys_exit(1);  /* Exit with error status */
}
sys_exit(0);  /* Normal exit */
```

### Cooperative Yielding
```c
while (1) {
    do_work();
    sys_yield();  /* Give other processes a chance */
}
```

## System Call Table

```c
typedef uint64_t (*syscall_fn_t)(uint64_t, uint64_t, uint64_t,
                                  uint64_t, uint64_t, uint64_t);

static const syscall_fn_t syscall_table[MAX_SYSCALLS] = {
    [SYS_EXIT]   = sys_exit_impl,
    [SYS_WRITE]  = sys_write_impl,
    [SYS_READ]   = sys_read_impl,
    [SYS_GETPID] = sys_getpid_impl,
    [SYS_YIELD]  = sys_yield_impl,
    /* Rest are NULL */
};
```

Sparse array allows gaps in syscall numbering.

## Statistics

```c
typedef struct {
    uint64_t total_syscalls;
    uint64_t syscall_counts[MAX_SYSCALLS];
} syscall_stats_t;
```

Tracks:
- Total number of syscalls made
- Per-syscall invocation counts

## Known Issues

### No SVC Dispatch
There is no SVC trap on the live path; syscalls are direct function calls. The synchronous vector decodes the SVC class for diagnostics, but no syscall flows through it. SVC-based dispatch is reserved for the future EL0 work described above.

### No User Space
Without user space (EL0), a trap-based syscall boundary is not needed. The numeric dispatcher and table exist so that adding EL0 later is a smaller change.

### Limited Error Codes
System calls return -1 or 0 for errors, not errno codes. No global errno variable.

### No Read Implementation
SYS_READ always returns 0 (EOF). Console input goes through `uart_getc()` directly in shell.

### File Descriptor Validation
sys_write only accepts stdout/stderr. Should eventually use VFS to validate and route to correct file.

## Testing

### Syscall Table Validation
```c
/* Verify all implemented syscalls are registered */
assert(syscall_table[SYS_EXIT] != NULL);
assert(syscall_table[SYS_WRITE] != NULL);
assert(syscall_table[SYS_GETPID] != NULL);
```

### Error Handling
```c
/* NULL pointer should return error */
uint64_t ret = sys_write(STDOUT_FILENO, NULL, 10);
assert(ret == (uint64_t)-1);

/* Invalid FD should return error */
ret = sys_write(99, "test", 4);
assert(ret == (uint64_t)-1);
```

### PID Uniqueness
```c
uint64_t pid1 = sys_getpid();
process_create(worker, "worker");
/* In worker: */
uint64_t pid2 = sys_getpid();
assert(pid1 != pid2);
```

## Future Enhancements

- User space (EL0) with proper SVC dispatch
- More syscalls: open, close, read, fork, exec, wait
- errno-style error reporting
- Syscall tracing and auditing
- Permission checks
- Resource limits