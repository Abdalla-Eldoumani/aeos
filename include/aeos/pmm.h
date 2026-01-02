/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/pmm.h
 * Description: Physical Memory Manager (Buddy Allocator) interface
 * ============================================================================ */

#ifndef AEOS_PMM_H
#define AEOS_PMM_H

#include <aeos/types.h>
#include <aeos/mm.h>

/* Maximum buddy order (2^MAX_ORDER pages) */
#define PMM_MAX_ORDER   10          /* Up to 2^10 = 1024 pages = 4MB */

/* Minimum allocation order */
#define PMM_MIN_ORDER   0           /* Single page (4KB) */

/**
 * Physical memory statistics
 */
typedef struct {
    size_t total_pages;         /* Total physical pages */
    size_t free_pages;          /* Currently free pages */
    size_t used_pages;          /* Currently used pages */
    size_t reserved_pages;      /* Reserved pages (kernel, etc.) */
} pmm_stats_t;

/**
 * Initialize the Physical Memory Manager
 *
 * @param mem_start Start of usable physical memory
 * @param mem_end End of usable physical memory
 * @param kernel_end End of kernel in physical memory
 *
 * This function:
 * - Sets up the buddy allocator data structures
 * - Marks kernel memory as reserved
 * - Makes remaining memory available for allocation
 */
void pmm_init(uint64_t mem_start, uint64_t mem_end, uint64_t kernel_end);

/**
 * Allocate 2^order contiguous physical pages
 *
 * @param order Allocation order (0 = 1 page, 1 = 2 pages, etc.)
 * @return Physical address of allocated memory, or 0 on failure
 *
 * Uses buddy allocation algorithm:
 * - If block of requested size is available, return it
 * - Otherwise, split larger block recursively
 * - O(log n) time complexity
 */
uint64_t pmm_alloc_pages(uint32_t order);

/**
 * Allocate a single physical page
 *
 * @return Physical address of allocated page, or 0 on failure
 */
static inline uint64_t pmm_alloc_page(void) {
    return pmm_alloc_pages(0);
}

/**
 * Free 2^order contiguous physical pages
 *
 * @param addr Physical address of memory to free
 * @param order Allocation order (must match allocation)
 *
 * Uses buddy deallocation:
 * - Mark block as free
 * - Merge with buddy if both are free
 * - Continue merging up the tree
 * - O(log n) time complexity
 */
void pmm_free_pages(uint64_t addr, uint32_t order);

/**
 * Free a single physical page
 *
 * @param addr Physical address of page to free
 */
static inline void pmm_free_page(uint64_t addr) {
    pmm_free_pages(addr, 0);
}

/**
 * Reserve a range of physical memory (mark as unavailable)
 *
 * @param start Start address (must be page-aligned)
 * @param end End address (must be page-aligned)
 *
 * Used for:
 * - Kernel image
 * - Memory-mapped I/O regions
 * - Reserved by firmware
 */
void pmm_reserve_region(uint64_t start, uint64_t end);

/**
 * Get physical memory statistics
 *
 * @param stats Pointer to stats structure to fill
 */
void pmm_get_stats(pmm_stats_t *stats);

/**
 * Print physical memory allocator state (for debugging)
 */
void pmm_dump_state(void);

#endif /* AEOS_PMM_H */
