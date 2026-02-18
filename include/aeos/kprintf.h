/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/kprintf.h
 * Description: Kernel printf interface for formatted output
 * ============================================================================ */

#ifndef AEOS_KPRINTF_H
#define AEOS_KPRINTF_H

#include <aeos/types.h>

/* Log levels for kernel messages */
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3,
    LOG_FATAL = 4
} log_level_t;

/**
 * Kernel printf - formatted output to console
 * Supports: %d, %u, %x, %X, %p, %s, %c, %%
 *
 * @param fmt Format string
 * @param ... Variable arguments
 * @return Number of characters printed
 */
int kprintf(const char *fmt, ...);

/**
 * Kernel log with severity level
 * Prepends log level prefix to message
 *
 * @param level Log severity level
 * @param fmt Format string
 * @param ... Variable arguments
 */
void klog(log_level_t level, const char *fmt, ...);

/**
 * Convenience macros for logging at different levels
 * Note: klog_debug only prints when DEBUG_ENABLED is defined (use DEBUG=1 make run)
 */
#ifdef DEBUG_ENABLED
#define klog_debug(fmt, ...) klog(LOG_DEBUG, fmt, ##__VA_ARGS__)
#else
#define klog_debug(fmt, ...) do {} while(0)
#endif

#define klog_info(fmt, ...)  klog(LOG_INFO, fmt, ##__VA_ARGS__)
#define klog_warn(fmt, ...)  klog(LOG_WARN, fmt, ##__VA_ARGS__)
#define klog_error(fmt, ...) klog(LOG_ERROR, fmt, ##__VA_ARGS__)
#define klog_fatal(fmt, ...) klog(LOG_FATAL, fmt, ##__VA_ARGS__)

/**
 * Output hook for redirecting kprintf output (e.g., to GUI terminal)
 * When set, each character goes to the hook instead of UART.
 * Set to NULL to restore normal UART output.
 */
typedef void (*kprintf_hook_fn)(char c);
extern kprintf_hook_fn kprintf_output_hook;

#endif /* AEOS_KPRINTF_H */
