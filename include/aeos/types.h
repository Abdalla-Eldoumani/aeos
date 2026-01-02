/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/types.h
 * Description: Common type definitions for the kernel
 * ============================================================================ */

#ifndef AEOS_TYPES_H
#define AEOS_TYPES_H

/* Standard integer types */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

/* Size types */
typedef unsigned long long size_t;
typedef signed long long   ssize_t;

/* Pointer types */
typedef unsigned long long uintptr_t;
typedef signed long long   intptr_t;

/* Boolean type */
typedef int bool;
#define true  1
#define false 0

/* NULL pointer */
#define NULL ((void *)0)

#endif /* AEOS_TYPES_H */
