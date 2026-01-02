/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/kernel/kprintf.c
 * Description: Kernel printf implementation for formatted output
 * ============================================================================ */

#include <aeos/kprintf.h>
#include <aeos/uart.h>
#include <aeos/types.h>

/* Variable argument list support */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

/* Helper function to print a single character */
static void putchar(char c)
{
    uart_putc(c);
}

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
 */
int kprintf(const char *fmt, ...)
{
    va_list args;
    int count = 0;
    int width = 0;
    int left_align = 0;
    int long_long = 0;
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

                    /* Handle width and alignment */
                    if (width > 0 && width > str_len) {
                        padding = width - str_len;
                        if (left_align) {
                            count += putstring(str);
                            for (i = 0; i < padding; i++) {
                                putchar(' ');
                                count++;
                            }
                        } else {
                            for (i = 0; i < padding; i++) {
                                putchar(' ');
                                count++;
                            }
                            count += putstring(str);
                        }
                    } else {
                        count += putstring(str);
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

                    /* Handle width and alignment */
                    if (width > 0 && width > str_len) {
                        padding = width - str_len;
                        if (left_align) {
                            putstring(str);
                            for (i = 0; i < padding; i++) {
                                putchar(' ');
                            }
                        } else {
                            for (i = 0; i < padding; i++) {
                                putchar(' ');
                            }
                            putstring(str);
                        }
                    } else {
                        putstring(str);
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
