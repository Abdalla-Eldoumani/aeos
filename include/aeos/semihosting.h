/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/semihosting.h
 * Description: ARM semihosting interface for host file I/O
 * ============================================================================ */

#ifndef AEOS_SEMIHOSTING_H
#define AEOS_SEMIHOSTING_H

#include <aeos/types.h>

/* Semihosting operation codes (prefixed with SEMI_ to avoid syscall.h conflicts) */
#define SEMI_SYS_OPEN        0x01    /* Open a file */
#define SEMI_SYS_CLOSE       0x02    /* Close a file */
#define SEMI_SYS_WRITEC      0x03    /* Write a character */
#define SEMI_SYS_WRITE0      0x04    /* Write a null-terminated string */
#define SEMI_SYS_WRITE       0x05    /* Write to a file */
#define SEMI_SYS_READ        0x06    /* Read from a file */
#define SEMI_SYS_READC       0x07    /* Read a character */
#define SEMI_SYS_ISERROR     0x08    /* Check if a value is an error */
#define SEMI_SYS_ISTTY       0x09    /* Check if a file is a terminal */
#define SEMI_SYS_SEEK        0x0A    /* Seek within a file */
#define SEMI_SYS_FLEN        0x0C    /* Get file length */
#define SEMI_SYS_TMPNAM      0x0D    /* Get temporary filename */
#define SEMI_SYS_REMOVE      0x0E    /* Remove a file */
#define SEMI_SYS_RENAME      0x0F    /* Rename a file */
#define SEMI_SYS_CLOCK       0x10    /* Get elapsed time */
#define SEMI_SYS_TIME        0x11    /* Get current time */
#define SEMI_SYS_SYSTEM      0x12    /* Execute system command */
#define SEMI_SYS_ERRNO       0x13    /* Get errno */
#define SEMI_SYS_GET_CMDLINE 0x15    /* Get command line */
#define SEMI_SYS_HEAPINFO    0x16    /* Get heap info */
#define SEMI_SYS_EXIT        0x18    /* Exit program */

/* File open modes (semihosting uses these values, not POSIX) */
#define SEMIHOST_OPEN_R     0   /* Read only */
#define SEMIHOST_OPEN_RB    1   /* Read only binary */
#define SEMIHOST_OPEN_RP    2   /* Read/write */
#define SEMIHOST_OPEN_RPB   3   /* Read/write binary */
#define SEMIHOST_OPEN_W     4   /* Write only (create/truncate) */
#define SEMIHOST_OPEN_WB    5   /* Write only binary */
#define SEMIHOST_OPEN_WP    6   /* Read/write (create/truncate) */
#define SEMIHOST_OPEN_WPB   7   /* Read/write binary (create/truncate) */
#define SEMIHOST_OPEN_A     8   /* Append (create if needed) */
#define SEMIHOST_OPEN_AB    9   /* Append binary */
#define SEMIHOST_OPEN_AP    10  /* Read/append */
#define SEMIHOST_OPEN_APB   11  /* Read/append binary */

/**
 * Initialize semihosting
 * Tests if semihosting is available
 *
 * @return 0 on success, -1 if semihosting not available
 */
int semihost_init(void);

/**
 * Check if semihosting is available
 *
 * @return true if available, false otherwise
 */
bool semihost_available(void);

/**
 * Open a file on the host
 *
 * @param path File path on host
 * @param mode Semihosting open mode (SEMIHOST_OPEN_*)
 * @return File handle (>=0) on success, -1 on error
 */
int semihost_open(const char *path, int mode);

/**
 * Close a file
 *
 * @param fd File handle from semihost_open
 * @return 0 on success, -1 on error
 */
int semihost_close(int fd);

/**
 * Write to a file
 *
 * @param fd File handle
 * @param buf Data to write
 * @param len Number of bytes to write
 * @return Number of bytes NOT written (0 = success), or original len on error
 */
size_t semihost_write(int fd, const void *buf, size_t len);

/**
 * Read from a file
 *
 * @param fd File handle
 * @param buf Buffer to read into
 * @param len Maximum bytes to read
 * @return Number of bytes NOT read (0 = all read), or len on error
 */
size_t semihost_read(int fd, void *buf, size_t len);

/**
 * Seek within a file
 *
 * @param fd File handle
 * @param pos Absolute position to seek to
 * @return 0 on success, -1 on error
 */
int semihost_seek(int fd, size_t pos);

/**
 * Get file length
 *
 * @param fd File handle
 * @return File length in bytes, or -1 on error
 */
ssize_t semihost_flen(int fd);

/**
 * Remove a file on the host
 *
 * @param path File path
 * @return 0 on success, -1 on error
 */
int semihost_remove(const char *path);

#endif /* AEOS_SEMIHOSTING_H */
