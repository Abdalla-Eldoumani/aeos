/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/syscall.h
 * Description: System call interface and numbers
 * ============================================================================ */

#ifndef AEOS_SYSCALL_H
#define AEOS_SYSCALL_H

#include <aeos/types.h>

/* System call numbers */
#define SYS_EXIT       0   /* Exit current process */
#define SYS_WRITE      1   /* Write to file descriptor */
#define SYS_READ       2   /* Read from file descriptor */
#define SYS_GETPID     3   /* Get process ID */
#define SYS_YIELD      4   /* Yield CPU to next process */
#define SYS_SLEEP      5   /* Sleep for N milliseconds (future) */
#define SYS_FORK       6   /* Fork process (future) */
#define SYS_EXEC       7   /* Execute program (future) */
#define SYS_WAIT       8   /* Wait for child process (future) */
#define SYS_OPEN       9   /* Open file (future) */
#define SYS_CLOSE      10  /* Close file (future) */

#define MAX_SYSCALLS   32  /* Maximum number of system calls */

/* Standard file descriptors */
#define STDIN_FILENO   0
#define STDOUT_FILENO  1
#define STDERR_FILENO  2

/*
 * System call handler (called from assembly)
 * Arguments are passed in x0-x5, syscall number in x8
 * Return value in x0
 */
uint64_t syscall_handler(uint64_t syscall_num,
                         uint64_t arg0, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5);

/*
 * Initialize syscall subsystem
 */
void syscall_init(void);

/*
 * Individual syscall implementations (kernel-side)
 */
uint64_t sys_exit(int status);
uint64_t sys_write(int fd, const void *buf, size_t count);
uint64_t sys_read(int fd, void *buf, size_t count);
uint64_t sys_getpid(void);
uint64_t sys_yield(void);

/*
 * User-space syscall wrappers (inline assembly)
 * These are used by user programs to invoke system calls
 */

static inline uint64_t syscall0(uint64_t num)
{
    /* Direct function call instead of SVC (kernel threads at EL1) */
    extern uint64_t syscall_handler(uint64_t syscall_num,
                                     uint64_t arg0, uint64_t arg1, uint64_t arg2,
                                     uint64_t arg3, uint64_t arg4, uint64_t arg5);
    return syscall_handler(num, 0, 0, 0, 0, 0, 0);
}

static inline uint64_t syscall1(uint64_t num, uint64_t arg0)
{
    /* Direct function call instead of SVC (kernel threads at EL1) */
    extern uint64_t syscall_handler(uint64_t syscall_num,
                                     uint64_t arg0, uint64_t arg1, uint64_t arg2,
                                     uint64_t arg3, uint64_t arg4, uint64_t arg5);
    return syscall_handler(num, arg0, 0, 0, 0, 0, 0);
}

static inline uint64_t syscall3(uint64_t num, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    /* Direct function call instead of SVC (kernel threads at EL1) */
    extern uint64_t syscall_handler(uint64_t syscall_num,
                                     uint64_t arg0, uint64_t arg1, uint64_t arg2,
                                     uint64_t arg3, uint64_t arg4, uint64_t arg5);
    return syscall_handler(num, arg0, arg1, arg2, 0, 0, 0);
}

/*
 * User-friendly syscall wrappers
 */
static inline void exit(int status)
{
    syscall1(SYS_EXIT, (uint64_t)status);
    __builtin_unreachable();
}

static inline ssize_t write(int fd, const void *buf, size_t count)
{
    return (ssize_t)syscall3(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

static inline ssize_t read(int fd, void *buf, size_t count)
{
    return (ssize_t)syscall3(SYS_READ, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

static inline uint64_t getpid(void)
{
    return syscall0(SYS_GETPID);
}

/* Note: yield() syscall wrapper removed to avoid conflict with kernel yield()
 * User programs can call syscall0(SYS_YIELD) directly or use kernel yield()
 */

#endif /* AEOS_SYSCALL_H */

/* ============================================================================
 * End of syscall.h
 * ============================================================================ */
