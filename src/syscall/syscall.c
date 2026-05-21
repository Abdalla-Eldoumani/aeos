/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/syscall/syscall.c
 * Description: System call dispatcher and implementations
 * ============================================================================ */

#include <aeos/syscall.h>
#include <aeos/process.h>
#include <aeos/scheduler.h>
#include <aeos/kprintf.h>
#include <aeos/uart.h>
#include <aeos/usermode.h>
#include <aeos/exec.h>
#include <aeos/types.h>

/* System call statistics */
static struct {
    uint64_t total_syscalls;
    uint64_t syscall_counts[MAX_SYSCALLS];
} syscall_stats;

/* Forward declarations of syscall implementations
 * All syscalls have the same signature for the syscall table
 */
static uint64_t sys_exit_impl(uint64_t arg0, uint64_t arg1, uint64_t arg2,
                               uint64_t arg3, uint64_t arg4, uint64_t arg5);
static uint64_t sys_write_impl(uint64_t arg0, uint64_t arg1, uint64_t arg2,
                                uint64_t arg3, uint64_t arg4, uint64_t arg5);
static uint64_t sys_read_impl(uint64_t arg0, uint64_t arg1, uint64_t arg2,
                               uint64_t arg3, uint64_t arg4, uint64_t arg5);
static uint64_t sys_getpid_impl(uint64_t arg0, uint64_t arg1, uint64_t arg2,
                                 uint64_t arg3, uint64_t arg4, uint64_t arg5);
static uint64_t sys_yield_impl(uint64_t arg0, uint64_t arg1, uint64_t arg2,
                                uint64_t arg3, uint64_t arg4, uint64_t arg5);

/* System call table - maps syscall numbers to function pointers */
typedef uint64_t (*syscall_fn_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static const syscall_fn_t syscall_table[MAX_SYSCALLS] = {
    [SYS_EXIT]   = sys_exit_impl,
    [SYS_WRITE]  = sys_write_impl,
    [SYS_READ]   = sys_read_impl,
    [SYS_GETPID] = sys_getpid_impl,
    [SYS_YIELD]  = sys_yield_impl,
    /* Other syscalls are NULL (not implemented yet) */
};

/**
 * Initialize syscall subsystem
 */
void syscall_init(void)
{
    klog_info("Initializing system call subsystem...");

    /* Clear statistics */
    syscall_stats.total_syscalls = 0;
    for (int i = 0; i < MAX_SYSCALLS; i++) {
        syscall_stats.syscall_counts[i] = 0;
    }

    klog_info("System call subsystem initialized");
    klog_info("  Implemented syscalls: exit, write, read, getpid, yield");
}

/**
 * Main syscall handler - called from exception vector
 *
 * @param syscall_num System call number (from x8)
 * @param arg0-arg5   Arguments (from x0-x5)
 * @return Return value (goes to x0)
 */
uint64_t syscall_handler(uint64_t syscall_num,
                         uint64_t arg0, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    /* Validate syscall number */
    if (syscall_num >= MAX_SYSCALLS) {
        klog_error("syscall_handler: Invalid syscall number %u", (uint32_t)syscall_num);
        return (uint64_t)-1;  /* Return -1 for error */
    }

    /* Check if syscall is implemented */
    if (syscall_table[syscall_num] == NULL) {
        klog_error("syscall_handler: Unimplemented syscall %u", (uint32_t)syscall_num);
        return (uint64_t)-1;
    }

    /* Update statistics */
    syscall_stats.total_syscalls++;
    syscall_stats.syscall_counts[syscall_num]++;

    /* Log syscall (debug only) */
    klog_debug("syscall: %u from PID %u",
               (uint32_t)syscall_num,
               (uint32_t)process_current()->pid);

    /* The middle round-trip marker (criterion 4). Fires only for the EL0
     * one-shot - never on the GUI/shell direct-call path, which is not one-shot
     * - so the production serial shows enter-EL0 / svc-from-EL0 / return without
     * spamming every kernel-thread syscall. klog_debug above is off by default. */
    if (el0_oneshot_active()) {
        klog_info("svc %u from EL0", (uint32_t)syscall_num);
    }

    /* Dispatch to syscall implementation */
    return syscall_table[syscall_num](arg0, arg1, arg2, arg3, arg4, arg5);
}

/**
 * True iff [ptr, ptr+len) lies wholly inside the currently-mapped EL0 window
 * [usermode_map_base(), usermode_map_end()). Checks the whole range, not just
 * the start (RESEARCH Pitfall 4), and rejects the ptr+len unsigned overflow.
 * len==0 is allowed (no dereference). Callers gate this on el0_oneshot_active()
 * so the EL1 direct-call path (kernel pointers) is never range-checked.
 */
static bool is_user_range(uint64_t ptr, uint64_t len)
{
    uint64_t base = usermode_map_base();
    uint64_t top  = usermode_map_end();

    if (len == 0) {
        return true;
    }
    if (ptr < base) {
        return false;
    }
    if (ptr >= top) {
        return false;
    }
    if (ptr + len < ptr) {   /* unsigned overflow of the end address */
        return false;
    }
    if (ptr + len > top) {
        return false;
    }
    return true;
}

/* ============================================================================
 * System Call Implementations
 * ============================================================================ */

/**
 * sys_exit - Terminate the calling process
 *
 * @param arg0 Exit status code
 * @param arg1-5 Unused
 * @return Never returns
 */
static uint64_t sys_exit_impl(uint64_t arg0, uint64_t arg1, uint64_t arg2,
                               uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    int status = (int)arg0;

    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;

    /* EL0 one-shot: return control to the kernel by restoring the saved kernel
     * context, NOT through the vector tail's eret (which would go back to EL0).
     * usermode_return never returns. The scheduler/process_exit path below is
     * for kernel threads and is left unchanged. */
    if (el0_oneshot_active()) {
        klog_info("sys_exit: EL0 one-shot exiting with status %d", status);
        usermode_return();
    }

    klog_info("sys_exit: PID %u exiting with status %d",
              (uint32_t)process_current()->pid, status);

    /* Call process_exit (never returns) */
    process_exit();

    /* Should never reach here */
    return 0;
}

/**
 * sys_write - Write to a file descriptor
 *
 * @param arg0 File descriptor (0=stdin, 1=stdout, 2=stderr)
 * @param arg1 Buffer to write from
 * @param arg2 Number of bytes to write
 * @param arg3-5 Unused
 * @return Number of bytes written, or -1 on error
 */
static uint64_t sys_write_impl(uint64_t arg0, uint64_t arg1, uint64_t arg2,
                                uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    int fd = (int)arg0;
    const void *buf = (const void *)arg1;
    size_t count = (size_t)arg2;
    const char *str = (const char *)buf;
    size_t i;

    (void)arg3; (void)arg4; (void)arg5;

    /* Validate arguments */
    if (buf == NULL) {
        klog_error("sys_write: NULL buffer");
        return (uint64_t)-1;
    }

    /* For now, all file descriptors write to UART */
    /* In the future, we'll have a proper VFS layer */
    if (fd != STDOUT_FILENO && fd != STDERR_FILENO) {
        klog_error("sys_write: Invalid fd %d", fd);
        return (uint64_t)-1;
    }

    /* EL0-origin write: the user buffer is attacker-controlled, so reject any
     * [buf, buf+count) that is not wholly inside the mapped user window before
     * the kernel dereferences a byte (T-06-07). Gated STRICTLY on the one-shot
     * flag so the EL1 direct-call path (GUI/shell/test_runner) keeps passing
     * trusted kernel pointers unchecked. */
    if (el0_oneshot_active()) {
        if (!is_user_range((uint64_t)buf, (uint64_t)count)) {
            klog_error("sys_write: EL0 buffer %p +%u outside mapped user window",
                       buf, (uint32_t)count);
            return (uint64_t)-1;
        }
    }

    /* Write each character */
    for (i = 0; i < count; i++) {
        uart_putc(str[i]);
    }

    return count;  /* Return number of bytes written */
}

/**
 * sys_read - Read from a file descriptor
 *
 * @param arg0 File descriptor
 * @param arg1 Buffer to read into
 * @param arg2 Maximum number of bytes to read
 * @param arg3-5 Unused
 * @return Number of bytes read, or -1 on error
 */
static uint64_t sys_read_impl(uint64_t arg0, uint64_t arg1, uint64_t arg2,
                               uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    /* Not implemented yet - just return 0 (EOF) */
    (void)arg0; (void)arg1; (void)arg2;
    (void)arg3; (void)arg4; (void)arg5;

    klog_warn("sys_read: Not implemented yet");
    return 0;
}

#ifdef TEST_BUILD
/* Records the pid the most recent sys_getpid returned. test_el0_roundtrip clears
 * it, runs the EL0 round trip, then asserts it holds the current pid - a
 * deterministic proof the EL0 svc reached syscall_handler -> sys_getpid_impl. */
static uint64_t last_el0_getpid;

uint64_t syscall_test_last_getpid(void)
{
    return last_el0_getpid;
}
#endif /* TEST_BUILD */

/**
 * sys_getpid - Get current process ID
 *
 * @param arg0-5 Unused
 * @return Process ID
 */
static uint64_t sys_getpid_impl(uint64_t arg0, uint64_t arg1, uint64_t arg2,
                                 uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    process_t *proc = process_current();

    (void)arg0; (void)arg1; (void)arg2;
    (void)arg3; (void)arg4; (void)arg5;

#ifdef TEST_BUILD
    last_el0_getpid = proc->pid;
#endif

    return proc->pid;
}

/**
 * sys_yield - Yield CPU to next process
 *
 * @param arg0-5 Unused
 * @return Always returns 0
 */
static uint64_t sys_yield_impl(uint64_t arg0, uint64_t arg1, uint64_t arg2,
                                uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    (void)arg0; (void)arg1; (void)arg2;
    (void)arg3; (void)arg4; (void)arg5;

    /* Call scheduler's yield function */
    yield();
    return 0;
}

/* ============================================================================
 * Kernel-side syscall wrappers (for use within kernel code)
 * These just call the implementation directly without going through SVC
 * ============================================================================ */

uint64_t sys_exit(int status)
{
    return sys_exit_impl((uint64_t)status, 0, 0, 0, 0, 0);
}

uint64_t sys_write(int fd, const void *buf, size_t count)
{
    return sys_write_impl((uint64_t)fd, (uint64_t)buf, (uint64_t)count, 0, 0, 0);
}

uint64_t sys_read(int fd, void *buf, size_t count)
{
    return sys_read_impl((uint64_t)fd, (uint64_t)buf, (uint64_t)count, 0, 0, 0);
}

uint64_t sys_getpid(void)
{
    return sys_getpid_impl(0, 0, 0, 0, 0, 0);
}

uint64_t sys_yield(void)
{
    return sys_yield_impl(0, 0, 0, 0, 0, 0);
}

/* ============================================================================
 * End of syscall.c
 * ============================================================================ */
