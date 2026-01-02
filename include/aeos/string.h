/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/string.h
 * Description: String utility functions
 * ============================================================================ */

#ifndef AEOS_STRING_H
#define AEOS_STRING_H

#include <aeos/types.h>

/**
 * Get length of string
 */
size_t strlen(const char *str);

/**
 * Compare two strings
 * @return 0 if equal, <0 if s1 < s2, >0 if s1 > s2
 */
int strcmp(const char *s1, const char *s2);

/**
 * Compare up to n characters of two strings
 */
int strncmp(const char *s1, const char *s2, size_t n);

/**
 * Copy string (UNSAFE - no bounds checking)
 */
char *strcpy(char *dest, const char *src);

/**
 * Copy up to n characters from string
 */
char *strncpy(char *dest, const char *src, size_t n);

/**
 * Concatenate strings (UNSAFE - no bounds checking)
 */
char *strcat(char *dest, const char *src);

/**
 * Concatenate up to n characters
 */
char *strncat(char *dest, const char *src, size_t n);

/**
 * Find first occurrence of character in string
 */
char *strchr(const char *str, int c);

/**
 * Find last occurrence of character in string
 */
char *strrchr(const char *str, int c);

/**
 * Find first occurrence of substring in string
 */
char *strstr(const char *haystack, const char *needle);

/**
 * Tokenize string (reentrant version)
 */
char *strtok_r(char *str, const char *delim, char **saveptr);

/**
 * Copy memory
 */
void *memcpy(void *dest, const void *src, size_t n);

/**
 * Set memory to value
 */
void *memset(void *dest, int c, size_t n);

/**
 * Compare memory
 */
int memcmp(const void *s1, const void *s2, size_t n);

/**
 * Move memory (handles overlapping regions)
 */
void *memmove(void *dest, const void *src, size_t n);

#endif /* AEOS_STRING_H */

/* ============================================================================
 * End of string.h
 * ============================================================================ */
