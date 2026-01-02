/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/heap.h
 * Description: Kernel heap allocator interface
 * ============================================================================ */

#ifndef AEOS_HEAP_H
#define AEOS_HEAP_H

#include <aeos/types.h>

/**
 * Initialize the kernel heap
 *
 * @param heap_start Start address of heap region
 * @param heap_size Size of heap in bytes
 */
void heap_init(void *heap_start, size_t heap_size);

/**
 * Allocate memory from kernel heap
 *
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 */
void *kmalloc(size_t size);

/**
 * Allocate and zero-initialize memory from kernel heap
 *
 * @param nmemb Number of elements
 * @param size Size of each element
 * @return Pointer to allocated memory, or NULL on failure
 */
void *kcalloc(size_t nmemb, size_t size);

/**
 * Free memory allocated by kmalloc/kcalloc
 *
 * @param ptr Pointer to memory to free (NULL is ignored)
 */
void kfree(void *ptr);

/**
 * Resize allocated memory block
 *
 * @param ptr Pointer to existing allocation (or NULL)
 * @param new_size New size in bytes
 * @return Pointer to resized memory, or NULL on failure
 */
void *krealloc(void *ptr, size_t new_size);

/**
 * Get heap statistics
 */
typedef struct {
    size_t total_size;      /* Total heap size */
    size_t used_size;       /* Currently allocated */
    size_t free_size;       /* Currently free */
    size_t num_blocks;      /* Number of blocks */
    size_t num_allocs;      /* Total allocations */
    size_t num_frees;       /* Total frees */
} heap_stats_t;

/**
 * Get heap statistics
 *
 * @param stats Pointer to stats structure to fill
 */
void heap_get_stats(heap_stats_t *stats);

/**
 * Print heap state for debugging
 */
void heap_dump_state(void);

#endif /* AEOS_HEAP_H */
