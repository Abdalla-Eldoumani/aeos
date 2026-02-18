/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/lib/string.c
 * Description: String utility functions implementation
 * ============================================================================ */

#include <aeos/string.h>
#include <aeos/types.h>

/**
 * Get length of string
 */
size_t strlen(const char *str)
{
    size_t len = 0;

    if (str == NULL) {
        return 0;
    }

    while (str[len] != '\0') {
        len++;
    }

    return len;
}

/**
 * Compare two strings
 */
int strcmp(const char *s1, const char *s2)
{
    if (s1 == NULL || s2 == NULL) {
        return (s1 == s2) ? 0 : (s1 == NULL ? -1 : 1);
    }

    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }

    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/**
 * Compare up to n characters
 */
int strncmp(const char *s1, const char *s2, size_t n)
{
    if (s1 == NULL || s2 == NULL || n == 0) {
        return 0;
    }

    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }

    if (n == 0) {
        return 0;
    }

    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/**
 * Copy string
 */
char *strcpy(char *dest, const char *src)
{
    char *d = dest;

    if (dest == NULL || src == NULL) {
        return dest;
    }

    while ((*d++ = *src++) != '\0') {
        /* Copy until null terminator */
    }

    return dest;
}

/**
 * Copy up to n characters
 */
char *strncpy(char *dest, const char *src, size_t n)
{
    size_t i;

    if (dest == NULL || src == NULL) {
        return dest;
    }

    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }

    /* Pad with nulls if src is shorter than n */
    for (; i < n; i++) {
        dest[i] = '\0';
    }

    return dest;
}

/**
 * Concatenate strings
 */
char *strcat(char *dest, const char *src)
{
    char *d = dest;

    if (dest == NULL || src == NULL) {
        return dest;
    }

    /* Find end of dest */
    while (*d != '\0') {
        d++;
    }

    /* Copy src to end of dest */
    while ((*d++ = *src++) != '\0') {
        /* Copy until null terminator */
    }

    return dest;
}

/**
 * Concatenate up to n characters
 */
char *strncat(char *dest, const char *src, size_t n)
{
    char *d = dest;
    size_t i;

    if (dest == NULL || src == NULL) {
        return dest;
    }

    /* Find end of dest */
    while (*d != '\0') {
        d++;
    }

    /* Copy up to n characters from src */
    for (i = 0; i < n && src[i] != '\0'; i++) {
        d[i] = src[i];
    }

    /* Null terminate */
    d[i] = '\0';

    return dest;
}

/**
 * Find first occurrence of character
 */
char *strchr(const char *str, int c)
{
    if (str == NULL) {
        return NULL;
    }

    while (*str != '\0') {
        if (*str == (char)c) {
            return (char *)str;
        }
        str++;
    }

    /* Check for null terminator match */
    if ((char)c == '\0') {
        return (char *)str;
    }

    return NULL;
}

/**
 * Find last occurrence of character
 */
char *strrchr(const char *str, int c)
{
    const char *last = NULL;

    if (str == NULL) {
        return NULL;
    }

    while (*str != '\0') {
        if (*str == (char)c) {
            last = str;
        }
        str++;
    }

    /* Check for null terminator match */
    if ((char)c == '\0') {
        return (char *)str;
    }

    return (char *)last;
}

/**
 * Find first occurrence of substring
 */
char *strstr(const char *haystack, const char *needle)
{
    size_t needle_len;
    size_t i;

    if (haystack == NULL || needle == NULL) {
        return NULL;
    }

    /* Empty needle matches at beginning */
    if (*needle == '\0') {
        return (char *)haystack;
    }

    needle_len = strlen(needle);

    while (*haystack != '\0') {
        /* Check if this position matches */
        for (i = 0; i < needle_len; i++) {
            if (haystack[i] != needle[i]) {
                break;
            }
        }

        /* Found match */
        if (i == needle_len) {
            return (char *)haystack;
        }

        haystack++;
    }

    return NULL;
}

/**
 * Tokenize string (reentrant version)
 */
char *strtok_r(char *str, const char *delim, char **saveptr)
{
    char *token;

    if (str != NULL) {
        *saveptr = str;
    }

    if (*saveptr == NULL) {
        return NULL;
    }

    /* Skip leading delimiters */
    while (**saveptr != '\0' && strchr(delim, **saveptr) != NULL) {
        (*saveptr)++;
    }

    if (**saveptr == '\0') {
        return NULL;
    }

    /* Start of token */
    token = *saveptr;

    /* Find end of token */
    while (**saveptr != '\0' && strchr(delim, **saveptr) == NULL) {
        (*saveptr)++;
    }

    /* Null-terminate token and advance saveptr */
    if (**saveptr != '\0') {
        **saveptr = '\0';
        (*saveptr)++;
    }

    return token;
}

/**
 * Copy memory
 */
void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    size_t i;

    if (dest == NULL || src == NULL) {
        return dest;
    }

    for (i = 0; i < n; i++) {
        d[i] = s[i];
    }

    return dest;
}

/**
 * Set memory to value
 */
void *memset(void *dest, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    size_t i;

    if (dest == NULL) {
        return dest;
    }

    for (i = 0; i < n; i++) {
        d[i] = (unsigned char)c;
    }

    return dest;
}

/**
 * Compare memory
 */
int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    size_t i;

    if (s1 == NULL || s2 == NULL) {
        return 0;
    }

    for (i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }

    return 0;
}

/**
 * Move memory (handles overlapping regions)
 */
void *memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    size_t i;

    if (dest == NULL || src == NULL || n == 0) {
        return dest;
    }

    if (d < s) {
        /* Copy forward */
        for (i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        /* Copy backward */
        for (i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }

    return dest;
}

/* Variable argument list support for snprintf */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

/**
 * Format integer to buffer with optional width and padding
 */
static int format_uint(char *buf, size_t size, size_t pos, uint32_t value, int base)
{
    char tmp[16];
    int i = 0;
    int count = 0;
    const char *digits = "0123456789abcdef";

    if (value == 0) {
        if (pos < size - 1) {
            buf[pos] = '0';
            count = 1;
        }
        return count;
    }

    while (value > 0 && i < 16) {
        tmp[i++] = digits[value % base];
        value /= base;
    }

    while (i > 0 && pos + count < size - 1) {
        buf[pos + count] = tmp[--i];
        count++;
    }

    return count;
}

/**
 * Format integer to buffer with width and zero-pad support
 */
static int format_uint_padded(char *buf, size_t size, size_t pos,
                              uint32_t value, int base, int width, char pad)
{
    char tmp[16];
    int i = 0;
    int count = 0;
    int padding;
    const char *digits = "0123456789abcdef";

    if (value == 0) {
        tmp[i++] = '0';
    } else {
        while (value > 0 && i < 16) {
            tmp[i++] = digits[value % base];
            value /= base;
        }
    }

    /* Add padding if needed */
    padding = width - i;
    while (padding > 0 && pos + count < size - 1) {
        buf[pos + count] = pad;
        count++;
        padding--;
    }

    /* Write digits */
    while (i > 0 && pos + count < size - 1) {
        buf[pos + count] = tmp[--i];
        count++;
    }

    return count;
}

/**
 * Simplified snprintf
 */
int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list args;
    size_t pos = 0;
    const char *str;
    int32_t val;
    uint32_t uval;

    if (buf == NULL || size == 0 || fmt == NULL) {
        return 0;
    }

    va_start(args, fmt);

    while (*fmt && pos < size - 1) {
        if (*fmt == '%') {
            fmt++;
            int width = 0;
            char pad_char = ' ';

            /* Check for zero-pad flag */
            if (*fmt == '0') {
                pad_char = '0';
                fmt++;
            }

            /* Parse width */
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }

            switch (*fmt) {
                case 's':
                    str = va_arg(args, const char *);
                    if (str == NULL) str = "(null)";
                    while (*str && pos < size - 1) {
                        buf[pos++] = *str++;
                    }
                    break;

                case 'd':
                    val = va_arg(args, int32_t);
                    if (val < 0) {
                        if (pos < size - 1) buf[pos++] = '-';
                        val = -val;
                    }
                    if (width > 0) {
                        pos += format_uint_padded(buf, size, pos, (uint32_t)val, 10, width, pad_char);
                    } else {
                        pos += format_uint(buf, size, pos, (uint32_t)val, 10);
                    }
                    break;

                case 'u':
                    uval = va_arg(args, uint32_t);
                    if (width > 0) {
                        pos += format_uint_padded(buf, size, pos, uval, 10, width, pad_char);
                    } else {
                        pos += format_uint(buf, size, pos, uval, 10);
                    }
                    break;

                case 'x':
                    uval = va_arg(args, uint32_t);
                    if (width > 0) {
                        pos += format_uint_padded(buf, size, pos, uval, 16, width, pad_char);
                    } else {
                        pos += format_uint(buf, size, pos, uval, 16);
                    }
                    break;

                case '%':
                    if (pos < size - 1) buf[pos++] = '%';
                    break;

                default:
                    if (pos < size - 1) buf[pos++] = '%';
                    if (pos < size - 1 && *fmt) buf[pos++] = *fmt;
                    break;
            }
            if (*fmt) fmt++;
        } else {
            buf[pos++] = *fmt++;
        }
    }

    buf[pos] = '\0';
    va_end(args);

    return (int)pos;
}

/* ============================================================================
 * End of string.c
 * ============================================================================ */
