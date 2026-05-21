# Section 05: System Calls

## Overview

This section implements the system call interface for AEOS. There are two entry paths into one dispatcher. Kernel code at EL1 makes a syscall as a direct C call (no privilege boundary to cross). An EL0 payload makes a syscall the architected way, with an `svc` trap into the EL1 vector table; that path was added in Phase 5 (FEAT-02) and is exercised and tested, though by a one-shot in-kernel payload rather than a scheduled process.

## Implementation Approach

### Direct Function Calls (the EL1 path)
A syscall is a table lookup followed by a direct C call. `syscall(num, args...)` validates `num` against the table size, fetches `syscall_table[num]`, and calls the handler as an ordinary function. The kernel-side wrappers (`sys_write`, `sys_getpid`, and so on) call the implementations directly:
```c
sys_write(STDOUT_FILENO, "Hello\n", 6);
```

The GUI and shell use this path. It stays because the kernel proper runs at EL1.

### SVC from EL0 (the trapped path)
An EL0 payload sets the syscall number in x8 and arguments in x0-x5 and executes `svc #0`:
```assembly
mov x8, #SYS_GETPID
svc #0
```
The instruction traps to `el0_aarch64_sync` (`src/interrupts/vectors.asm`, VBAR+0x400), which decodes ESR_EL1 EC=0x15, pulls x8/x0-x5 out of the saved register frame, and calls the SAME `syscall_handler`. The return value is written back into the frame's x0 slot and the handler `eret`s to EL0. The dispatcher is shared: the EL0 svc path was added alongside the direct-call path, not as a replacement.

The privilege boundary is real. The EL0 code page is mapped EL0-accessible (AP=01) while kernel pages stay AP=00 (EL0 no-access), so EL0 faults on any kernel address; a privileged instruction from EL0 (`msr daifset`) traps to EL1 with ESR EC=0x18 rather than executing. `make test` proves both directions: `test_el0_roundtrip` (the getpid+exit round trip returns control to the kernel and the getpid svc is observed) and `test_el0_priv_trap` (the privileged-instruction trap is EC=0x18).

What is NOT done yet: a scheduled EL0 process loaded from a file, per-section W^X, and user-pointer validation in syscalls (the one-shot payload passes no user pointers). Those are Phase 6.

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

    /* EL0 one-shot: hand control back to the kernel instead of the vector
     * tail's eret (which would return to EL0). Never returns. */
    if (el0_oneshot_active()) {
        usermode_return();
    }

    /* Kernel-thread path, unchanged. */
    process_exit();  /* Never returns */
    return 0;
}
```

Terminates the caller. For the EL0 one-shot it returns control to the kernel via `usermode_return` (restoring the saved kernel context and abandoning the svc exception frame); for a kernel thread it calls `process_exit`.

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

### No user-pointer validation
Syscalls do not range-check user-supplied pointers before dereferencing them at EL1. The EL0 one-shot payload passes no pointers (getpid, exit, daifset take none), so this is not exercised yet, but any future syscall that takes a user buffer must validate it with `IS_USER_ADDR` first. Phase 6.

### No scheduled EL0 process
EL0 execution today is a one-shot in-kernel payload entered from `kernel_main`, not a process loaded from a file and scheduled. The mechanism (entry trampoline, svc dispatch, user-page mapping) is in place; loading an ELF and running it under the scheduler is Phase 6.

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

- A scheduled EL0 process loaded from a file (the svc dispatch and entry path already exist; the ELF loader and per-process scheduling are next)
- User-pointer validation and per-section W^X for the EL0 path
- More syscalls: open, close, read, fork, exec, wait
- errno-style error reporting
- Syscall tracing and auditing
- Permission checks
- Resource limits