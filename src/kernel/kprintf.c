/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/kernel/kprintf.c
 * Description: Kernel printf implementation for formatted output
 * ============================================================================ */

#include <aeos/kprintf.h>
#include <aeos/uart.h>
#include <aeos/types.h>
#include <aeos/spinlock.h>

/* Variable argument list support */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

/* Output hook for redirecting kprintf output (e.g., to GUI terminal) */
kprintf_hook_fn kprintf_output_hook = NULL;

/* Crash log ring buffer.
 *
 * Every char emitted through kprintf/klog (regardless of whether the GUI
 * terminal hook is intercepting it) also lands here. On panic, the
 * exception handler dumps the ring to /crash.log via semihosting so post
 * mortems include the boot log, the EXCEPTION block, and the backtrace
 * even when the framebuffer is gone.
 *
 * Cross-core interleave invariant (SEC-04, discharged in Phase 7).
 *
 * The ring is written from exactly one place, putchar, and read from exactly
 * one place, the panic path, which reaches kprintf_ring_walk from
 * handle_exception. Two writers (two cores both in putchar) or a writer racing
 * the panic reader would splice a half-updated (pos, wrapped) pair into the
 * crash log.
 *
 * TWO layers of exclusion, both needed under SMP:
 *
 *   1. handle_exception masks DAIF on entry (exceptions.c, the BUG-15 fix). On
 *      ONE core this stops a timer IRQ from preempting a normal-mode ring write
 *      while the panic path reads the same buffer. This stays - it is the
 *      same-core exclusion. But with more than one core, masking this core's
 *      interrupts does nothing about the others.
 *   2. kprintf_ring_lock (added here). putchar takes it across the ring store +
 *      pos/wrap update so two cores cannot splice a torn (pos, wrapped) pair.
 *      This is the cross-core exclusion the single-CPU DAIF-mask invariant could
 *      not provide - the lock SEC-04 deferred to the SMP phase. It is ADDITIVE:
 *      it does not replace the DAIF mask, it adds the cross-core ordering on top.
 *
 * The panic reader (kprintf_ring_walk) does NOT block on the lock. It uses
 * spin_trylock-or-bypass: it tries the lock and reads the ring whether or not it
 * got it. A faulted/wedged core might hold kprintf_ring_lock forever (it faulted
 * mid-write), and a blocking acquire there would deadlock the panic so the crash
 * dump prints NOTHING. A slightly-torn read during a panic is acceptable; a
 * silent panic is not. test_kprintf_ring_panic_bypass makes this no-deadlock
 * property an automated gate.
 *
 * putchar holds the lock only across the short ring store (not across uart_putc,
 * the hook, or any re-entrant call), so the same-core panic sequence
 * (handle_exception -> kprintf -> putchar lock/unlock, THEN kprintf_ring_walk
 * trylock) never holds the lock across the re-entry into kprintf_ring_walk and
 * so cannot self-deadlock on this non-recursive lock. */
#define KPRINTF_RING_SIZE 4096
static char       kprintf_ring[KPRINTF_RING_SIZE];
static uint32_t   kprintf_ring_pos = 0;
static bool       kprintf_ring_wrapped = false;
static spinlock_t kprintf_ring_lock = SPINLOCK_INIT;

/* Helper function to print a single character */
static void putchar(char c)
{
    /* The emit (the GUI hook or uart_putc) is the slow part and touches no
     * shared ring state, so it stays OUTSIDE the lock. The shared state is only
     * the ring buffer + (pos, wrapped) pair. */
    if (kprintf_output_hook) {
        kprintf_output_hook(c);
    } else {
        uart_putc(c);
    }

    /* Serialize the ring store + pos/wrap update across cores: two cores both
     * in putchar would otherwise splice a half-updated (pos, wrapped) pair into
     * the ring (SEC-04 under SMP). The window is intentionally SHORT - just the
     * store and the index update, never across the emit above or a re-entrant
     * call - so the same-core panic sequence (kprintf -> putchar here, THEN
     * kprintf_ring_walk's trylock) never holds this non-recursive lock across
     * the re-entry. The unlock's stlr provides the release ordering a panic
     * reader needs (the byte store is globally visible before the lock frees);
     * the dmb ish is kept as the documented SEC-04 store/index ordering for a
     * reader that observes the ring without taking the lock (the trylock-bypass
     * path), where the stlr does not order against it. */
    spin_lock(&kprintf_ring_lock);
    kprintf_ring[kprintf_ring_pos++] = c;
    if (kprintf_ring_pos >= KPRINTF_RING_SIZE) {
        kprintf_ring_pos = 0;
        kprintf_ring_wrapped = true;
    }
    __asm__ volatile("dmb ish" ::: "memory");
    spin_unlock(&kprintf_ring_lock);
}

void kprintf_ring_walk(kprintf_ring_sink_fn sink)
{
    if (sink == NULL) {
        return;
    }

    /* PANIC-PATH SAFETY (the load-bearing claim): try the ring lock, but read
     * the ring WHETHER OR NOT we get it. The crash dump's job is to print; a
     * faulted/wedged core may hold kprintf_ring_lock forever (it faulted
     * mid-write), so a blocking spin_lock here would deadlock the panic and the
     * dump would print NOTHING. A blocking acquire is also wrong on the same
     * core: handle_exception's kprintf -> putchar runs before this, and if that
     * lock were ever held into here it would self-deadlock on the non-recursive
     * lock. trylock-or-bypass guarantees forward progress: a slightly-torn read
     * during a panic is acceptable; a silent panic is not. The trylock only
     * suppresses a torn read in the uncontended case. test_kprintf_ring_panic_-
     * bypass makes this no-deadlock property a RED/GREEN gate. */
    int got = spin_trylock(&kprintf_ring_lock);

    if (kprintf_ring_wrapped) {
        /* Older half: from current write position to end of buffer. */
        sink(kprintf_ring + kprintf_ring_pos,
             KPRINTF_RING_SIZE - kprintf_ring_pos);
    }
    if (kprintf_ring_pos > 0) {
        /* Newest half: from start of buffer to current write position. */
        sink(kprintf_ring, kprintf_ring_pos);
    }

    if (got) {
        spin_unlock(&kprintf_ring_lock);
    }
}

#ifdef TEST_BUILD
/* A sink that discards. The panic-bypass gate only needs to prove
 * kprintf_ring_walk RETURNS while the ring lock is held; it does not validate
 * the dump contents, so a no-op sink keeps the seam free of any semihosting
 * crash-file write (backtrace.c's real sink) while still driving the full
 * trylock-or-bypass path (the trylock attempt, the read, the unlock-if-got). */
static void kprintf_test_discard_sink(const char *buf, uint32_t len)
{
    (void)buf;
    (void)len;
}

int kprintf_test_panic_bypass_returns(void)
{
    /* Hold the ring lock on THIS core, then enter the panic reader. Its trylock
     * fails on the held lock and it bypasses (reads anyway, returns). If it used
     * a blocking spin_lock it would never return here - this same core already
     * holds the lock - and the runner would hang to its timeout (the gate's RED
     * behavior). Reaching `return 1` is the proof the bypass does not deadlock. */
    spin_lock(&kprintf_ring_lock);
    kprintf_ring_walk(kprintf_test_discard_sink);
    spin_unlock(&kprintf_ring_lock);
    return 1;
}
#endif /* TEST_BUILD */

/* Helper function to print a string */
static int putstring(const char *s)
{
    int count = 0;

    if (s == NULL) {
        uart_puts("(null)");
        return 6;
    }

    while (*s) {
        putchar(*s++);
        count++;
    }

    return count;
}

/* Helper function to print an unsigned integer in a given base */
static int print_uint(uint64_t value, int base, int uppercase)
{
    char buf[32];
    int i = 0;
    int count = 0;
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    /* Handle zero specially */
    if (value == 0) {
        putchar('0');
        return 1;
    }

    /* Convert to string (reversed) */
    while (value > 0) {
        buf[i++] = digits[value % base];
        value /= base;
    }

    /* Print in correct order */
    while (i > 0) {
        putchar(buf[--i]);
        count++;
    }

    return count;
}

/* Helper function to print a signed integer */
static int print_int(int64_t value)
{
    int count = 0;

    if (value < 0) {
        putchar('-');
        count++;
        value = -value;
    }

    count += print_uint((uint64_t)value, 10, 0);
    return count;
}

/* Helper function to print a pointer */
static int print_pointer(void *ptr)
{
    int count = 0;
    uint64_t addr = (uint64_t)ptr;

    /* Print "0x" prefix */
    putchar('0');
    putchar('x');
    count += 2;

    /* Print address in hex (16 digits for 64-bit) */
    char buf[16];
    int i;

    for (i = 15; i >= 0; i--) {
        buf[i] = "0123456789abcdef"[addr & 0xF];
        addr >>= 4;
    }

    for (i = 0; i < 16; i++) {
        putchar(buf[i]);
        count++;
    }

    return count;
}

/**
 * Kernel printf - formatted output to console
 * Supports: %d, %u, %x, %X, %llu, %lld, %p, %s, %c, %%
 * Supports width modifiers: %-10s, %10s
 * Supports precision on %s: %.40s caps the string at 40 chars
 */
int kprintf(const char *fmt, ...)
{
    va_list args;
    int count = 0;
    int width = 0;
    int left_align = 0;
    int long_long = 0;
    int precision;
    const char *str;
    int str_len, padding, i;

    if (fmt == NULL) {
        return 0;
    }

    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            width = 0;
            left_align = 0;
            long_long = 0;

            /* Check for left alignment '-' */
            if (*fmt == '-') {
                left_align = 1;
                fmt++;
            }

            /* Parse width */
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }

            /* Parse precision (e.g. %.40s caps the string at 40 chars) */
            precision = -1;
            if (*fmt == '.') {
                fmt++;
                precision = 0;
                while (*fmt >= '0' && *fmt <= '9') {
                    precision = precision * 10 + (*fmt - '0');
                    fmt++;
                }
            }

            /* Check for 'l' or 'll' modifier */
            if (*fmt == 'l') {
                fmt++;
                if (*fmt == 'l') {
                    long_long = 1;
                    fmt++;
                }
            }

            switch (*fmt) {
                case 'd':  /* Signed decimal */
                case 'i':
                    if (long_long) {
                        count += print_int(va_arg(args, int64_t));
                    } else {
                        count += print_int(va_arg(args, int));
                    }
                    break;

                case 'u':  /* Unsigned decimal */
                    if (long_long) {
                        count += print_uint(va_arg(args, uint64_t), 10, 0);
                    } else {
                        count += print_uint(va_arg(args, unsigned int), 10, 0);
                    }
                    break;

                case 'x':  /* Hexadecimal lowercase */
                    if (long_long) {
                        count += print_uint(va_arg(args, uint64_t), 16, 0);
                    } else {
                        count += print_uint(va_arg(args, unsigned int), 16, 0);
                    }
                    break;

                case 'X':  /* Hexadecimal uppercase */
                    if (long_long) {
                        count += print_uint(va_arg(args, uint64_t), 16, 1);
                    } else {
                        count += print_uint(va_arg(args, unsigned int), 16, 1);
                    }
                    break;

                case 'p':  /* Pointer */
                    count += print_pointer(va_arg(args, void *));
                    break;

                case 's':  /* String */
                    str = va_arg(args, const char *);
                    if (str == NULL) {
                        str = "(null)";
                    }
                    str_len = 0;
                    while (str[str_len] != '\0') {
                        str_len++;
                    }

                    /* Apply precision: cap output length at .N */
                    if (precision >= 0 && precision < str_len) {
                        str_len = precision;
                    }

                    /* Handle width and alignment */
                    if (width > 0 && width > str_len) {
                        padding = width - str_len;
                        if (left_align) {
                            for (i = 0; i < str_len; i++) {
                                putchar(str[i]);
                                count++;
                            }
                            for (i = 0; i < padding; i++) {
                                putchar(' ');
                                count++;
                            }
                        } else {
                            for (i = 0; i < padding; i++) {
                                putchar(' ');
                                count++;
                            }
                            for (i = 0; i < str_len; i++) {
                                putchar(str[i]);
                                count++;
                            }
                        }
                    } else {
                        for (i = 0; i < str_len; i++) {
                            putchar(str[i]);
                            count++;
                        }
                    }
                    break;

                case 'c':  /* Character */
                    putchar((char)va_arg(args, int));
                    count++;
                    break;

                case '%':  /* Literal % */
                    putchar('%');
                    count++;
                    break;

                default:
                    /* Unknown format specifier, print as-is */
                    putchar('%');
                    putchar(*fmt);
                    count += 2;
                    break;
            }

            fmt++;
        } else {
            putchar(*fmt++);
            count++;
        }
    }

    va_end(args);
    return count;
}

/**
 * Kernel log with severity level
 */
void klog(log_level_t level, const char *fmt, ...)
{
    va_list args;
    const char *prefix;
    int width = 0;
    int left_align = 0;
    int long_long = 0;
    int precision;
    const char *str;
    int str_len, padding, i;

    /* Select prefix based on log level */
    switch (level) {
        case LOG_DEBUG:
            prefix = "[DEBUG] ";
            break;
        case LOG_INFO:
            prefix = "[INFO]  ";
            break;
        case LOG_WARN:
            prefix = "[WARN]  ";
            break;
        case LOG_ERROR:
            prefix = "[ERROR] ";
            break;
        case LOG_FATAL:
            prefix = "[FATAL] ";
            break;
        default:
            prefix = "[?????] ";
            break;
    }

    /* Print prefix */
    putstring(prefix);

    /* Print formatted message */
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            width = 0;
            left_align = 0;
            long_long = 0;

            /* Check for left alignment '-' */
            if (*fmt == '-') {
                left_align = 1;
                fmt++;
            }

            /* Parse width */
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }

            /* Parse precision (e.g. %.40s caps the string at 40 chars) */
            precision = -1;
            if (*fmt == '.') {
                fmt++;
                precision = 0;
                while (*fmt >= '0' && *fmt <= '9') {
                    precision = precision * 10 + (*fmt - '0');
                    fmt++;
                }
            }

            /* Check for 'l' or 'll' modifier */
            if (*fmt == 'l') {
                fmt++;
                if (*fmt == 'l') {
                    long_long = 1;
                    fmt++;
                }
            }

            switch (*fmt) {
                case 'd':
                case 'i':
                    if (long_long) {
                        print_int(va_arg(args, int64_t));
                    } else {
                        print_int(va_arg(args, int));
                    }
                    break;
                case 'u':
                    if (long_long) {
                        print_uint(va_arg(args, uint64_t), 10, 0);
                    } else {
                        print_uint(va_arg(args, unsigned int), 10, 0);
                    }
                    break;
                case 'x':
                    if (long_long) {
                        print_uint(va_arg(args, uint64_t), 16, 0);
                    } else {
                        print_uint(va_arg(args, unsigned int), 16, 0);
                    }
                    break;
                case 'X':
                    if (long_long) {
                        print_uint(va_arg(args, uint64_t), 16, 1);
                    } else {
                        print_uint(va_arg(args, unsigned int), 16, 1);
                    }
                    break;
                case 'p':
                    print_pointer(va_arg(args, void *));
                    break;
                case 's':
                    str = va_arg(args, const char *);
                    if (str == NULL) {
                        str = "(null)";
                    }
                    str_len = 0;
                    while (str[str_len] != '\0') {
                        str_len++;
                    }

                    /* Apply precision: cap output length at .N */
                    if (precision >= 0 && precision < str_len) {
                        str_len = precision;
                    }

                    /* Handle width and alignment */
                    if (width > 0 && width > str_len) {
                        padding = width - str_len;
                        if (left_align) {
                            for (i = 0; i < str_len; i++) {
                                putchar(str[i]);
                            }
                            for (i = 0; i < padding; i++) {
                                putchar(' ');
                            }
                        } else {
                            for (i = 0; i < padding; i++) {
                                putchar(' ');
                            }
                            for (i = 0; i < str_len; i++) {
                                putchar(str[i]);
                            }
                        }
                    } else {
                        for (i = 0; i < str_len; i++) {
                            putchar(str[i]);
                        }
                    }
                    break;
                case 'c':
                    putchar((char)va_arg(args, int));
                    break;
                case '%':
                    putchar('%');
                    break;
                default:
                    putchar('%');
                    putchar(*fmt);
                    break;
            }

            fmt++;
        } else {
            putchar(*fmt++);
        }
    }

    va_end(args);

    /* Add newline */
    putchar('\n');
}

/* ============================================================================
 * End of kprintf.c
 * ============================================================================ */
