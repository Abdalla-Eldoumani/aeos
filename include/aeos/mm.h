/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/mm.h
 * Description: Memory management common definitions and constants
 * ============================================================================ */

#ifndef AEOS_MM_H
#define AEOS_MM_H

#include <aeos/types.h>

/* Page size and related constants */
#define PAGE_SIZE       4096        /* 4KB pages */
#define PAGE_SHIFT      12          /* log2(PAGE_SIZE) */
#define PAGE_MASK       (~(PAGE_SIZE - 1))

/* Memory regions for QEMU virt board */
#define PHYS_RAM_START  0x40000000  /* Physical RAM starts at 1GB */
#define PHYS_RAM_SIZE   0x10000000  /* 256MB default */
#define PHYS_RAM_END    (PHYS_RAM_START + PHYS_RAM_SIZE)

/* Kernel virtual memory layout */
#define KERNEL_VIRT_BASE    0xFFFF000000000000ULL  /* Kernel high half */
#define USER_VIRT_MAX       0x0000800000000000ULL  /* User space limit */

/* Convert between physical and virtual addresses */
#define PHYS_TO_VIRT(addr)  ((void *)((uint64_t)(addr) + KERNEL_VIRT_BASE))
#define VIRT_TO_PHYS(addr)  ((uint64_t)(addr) - KERNEL_VIRT_BASE)

/* Check if address is kernel or user */
#define IS_KERNEL_ADDR(addr) ((uint64_t)(addr) >= KERNEL_VIRT_BASE)
#define IS_USER_ADDR(addr)   ((uint64_t)(addr) < USER_VIRT_MAX)

/* Page alignment macros */
#define PAGE_ALIGN_DOWN(addr)   ((addr) & PAGE_MASK)
#define PAGE_ALIGN_UP(addr)     (((addr) + PAGE_SIZE - 1) & PAGE_MASK)
#define IS_PAGE_ALIGNED(addr)   (((addr) & (PAGE_SIZE - 1)) == 0)

/* Convert address to page frame number and vice versa */
#define ADDR_TO_PFN(addr)       ((addr) >> PAGE_SHIFT)
#define PFN_TO_ADDR(pfn)        ((pfn) << PAGE_SHIFT)

/* Number of pages in a memory region */
#define BYTES_TO_PAGES(bytes)   (((bytes) + PAGE_SIZE - 1) >> PAGE_SHIFT)

/* Memory protection flags (for future VMM use) */
#define MEM_READ    (1 << 0)
#define MEM_WRITE   (1 << 1)
#define MEM_EXEC    (1 << 2)
#define MEM_USER    (1 << 3)
#define MEM_NOCACHE (1 << 4)

/* Common memory protection combinations */
#define MEM_KERNEL_RO   (MEM_READ)
#define MEM_KERNEL_RW   (MEM_READ | MEM_WRITE)
#define MEM_KERNEL_RX   (MEM_READ | MEM_EXEC)
#define MEM_USER_RO     (MEM_READ | MEM_USER)
#define MEM_USER_RW     (MEM_READ | MEM_WRITE | MEM_USER)
#define MEM_USER_RX     (MEM_READ | MEM_EXEC | MEM_USER)

/**
 * Initialize all memory management subsystems
 * Must be called early in kernel initialization
 */
void mm_init(void);

/**
 * Get total available physical memory in bytes
 */
size_t mm_get_total_memory(void);

/**
 * Get free physical memory in bytes
 */
size_t mm_get_free_memory(void);

/**
 * Get used physical memory in bytes
 */
size_t mm_get_used_memory(void);

#endif /* AEOS_MM_H */
