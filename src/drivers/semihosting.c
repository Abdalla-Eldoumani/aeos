/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/drivers/semihosting.c
 * Description: ARM semihosting driver for host file I/O
 * ============================================================================ */

#include <aeos/semihosting.h>
#include <aeos/kprintf.h>
#include <aeos/string.h>

/* Semihosting availability flag */
static bool semihosting_enabled = false;

/* ============================================================================
 * Low-level Semihosting Call
 * ============================================================================ */

/**
 * Execute an ARM semihosting call
 * AArch64 uses HLT #0xF000 as the trap instruction
 *
 * @param op Semihosting operation code
 * @param arg Pointer to argument block (operation-specific)
 * @return Result value (operation-specific)
 */
static inline long semihosting_call(int op, void *arg)
{
    register long r0 __asm__("x0") = op;
    register void *r1 __asm__("x1") = arg;

    __asm__ volatile(
        "hlt #0xF000"
        : "+r"(r0)
        : "r"(r1)
        : "memory"
    );

    return r0;
}

/* ============================================================================
 * Semihosting API Implementation
 * ============================================================================ */

/**
 * Initialize semihosting
 * Tests if semihosting is available by attempting a benign operation
 */
int semihost_init(void)
{
    klog_info("Initializing ARM semihosting...");

    /* Try to get the current time - a safe test operation */
    /* If semihosting is not enabled, this will cause an exception */
    /* We assume QEMU is started with -semihosting-config enable=on */

    /* For now, just assume it's available if we're running in QEMU */
    /* A real implementation would catch the exception if it fails */
    semihosting_enabled = true;

    klog_info("Semihosting initialized (assuming QEMU with -semihosting-config)");
    return 0;
}

/**
 * Check if semihosting is available
 */
bool semihost_available(void)
{
    return semihosting_enabled;
}

/**
 * Open a file on the host
 */
int semihost_open(const char *path, int mode)
{
    struct {
        const char *path;
        int mode;
        size_t path_len;
    } args;

    if (!semihosting_enabled || path == NULL) {
        return -1;
    }

    args.path = path;
    args.mode = mode;
    args.path_len = strlen(path);

    return (int)semihosting_call(SEMI_SYS_OPEN, &args);
}

/**
 * Close a file
 */
int semihost_close(int fd)
{
    if (!semihosting_enabled || fd < 0) {
        return -1;
    }

    return (int)semihosting_call(SEMI_SYS_CLOSE, &fd);
}

/**
 * Write to a file
 * Returns number of bytes NOT written (0 = success)
 */
size_t semihost_write(int fd, const void *buf, size_t len)
{
    struct {
        int fd;
        const void *buf;
        size_t len;
    } args;

    if (!semihosting_enabled || fd < 0 || buf == NULL) {
        return len;  /* All bytes not written = error */
    }

    args.fd = fd;
    args.buf = buf;
    args.len = len;

    return (size_t)semihosting_call(SEMI_SYS_WRITE, &args);
}

/**
 * Read from a file
 * Returns number of bytes NOT read (0 = all read)
 */
size_t semihost_read(int fd, void *buf, size_t len)
{
    struct {
        int fd;
        void *buf;
        size_t len;
    } args;

    if (!semihosting_enabled || fd < 0 || buf == NULL) {
        return len;  /* All bytes not read = error */
    }

    args.fd = fd;
    args.buf = buf;
    args.len = len;

    return (size_t)semihosting_call(SEMI_SYS_READ, &args);
}

/**
 * Seek within a file
 */
int semihost_seek(int fd, size_t pos)
{
    struct {
        int fd;
        size_t pos;
    } args;

    if (!semihosting_enabled || fd < 0) {
        return -1;
    }

    args.fd = fd;
    args.pos = pos;

    return (int)semihosting_call(SEMI_SYS_SEEK, &args);
}

/**
 * Get file length
 */
ssize_t semihost_flen(int fd)
{
    if (!semihosting_enabled || fd < 0) {
        return -1;
    }

    return (ssize_t)semihosting_call(SEMI_SYS_FLEN, &fd);
}

/**
 * Remove a file on the host
 */
int semihost_remove(const char *path)
{
    struct {
        const char *path;
        size_t path_len;
    } args;

    if (!semihosting_enabled || path == NULL) {
        return -1;
    }

    args.path = path;
    args.path_len = strlen(path);

    return (int)semihosting_call(SEMI_SYS_REMOVE, &args);
}

/* ============================================================================
 * End of semihosting.c
 * ============================================================================ */
