/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/mm/heap.c
 * Description: Kernel heap allocator - Simple first-fit implementation
 * ============================================================================ */

#include <aeos/heap.h>
#include <aeos/types.h>
#include <aeos/kprintf.h>

/**
 * Block header for heap allocations
 * Stored before each allocated or free block
 */
typedef struct heap_block {
    size_t size;                /* Size of block (including header) */
    bool is_free;               /* True if block is free */
    struct heap_block *next;    /* Next block in list */
    struct heap_block *prev;    /* Previous block in list */
} heap_block_t;

#define BLOCK_HEADER_SIZE   sizeof(heap_block_t)
#define MIN_BLOCK_SIZE      (BLOCK_HEADER_SIZE + 16)

/**
 * Heap state
 */
static struct {
    void *heap_start;           /* Start of heap region */
    void *heap_end;             /* End of heap region */
    size_t heap_size;           /* Total heap size */
    heap_block_t *first_block;  /* First block in list */
    size_t num_allocs;          /* Total allocations */
    size_t num_frees;           /* Total frees */
    bool initialized;           /* Initialization flag */
} heap;

/* Forward declarations */
static heap_block_t *find_free_block(size_t size);
static void split_block(heap_block_t *block, size_t size);
static void merge_free_blocks(void);

/**
 * Initialize the kernel heap
 */
void heap_init(void *heap_start, size_t heap_size)
{
    heap_block_t *initial_block;

    klog_info("Initializing kernel heap...");

    /* Save heap bounds */
    heap.heap_start = heap_start;
    heap.heap_size = heap_size;
    heap.heap_end = (void *)((uint64_t)heap_start + heap_size);
    heap.num_allocs = 0;
    heap.num_frees = 0;

    /* Create initial free block spanning entire heap */
    initial_block = (heap_block_t *)heap_start;
    initial_block->size = heap_size;
    initial_block->is_free = true;
    initial_block->next = NULL;
    initial_block->prev = NULL;

    heap.first_block = initial_block;
    heap.initialized = true;

    kprintf("  Heap region: %p - %p\n", heap_start, heap.heap_end);
    kprintf("  Heap size: %u KB\n", (uint32_t)(heap_size / 1024));
    klog_info("Heap initialization complete");
}

/**
 * Allocate memory from kernel heap
 */
void *kmalloc(size_t size)
{
    heap_block_t *block;
    void *ptr;

    if (!heap.initialized) {
        klog_error("Heap not initialized");
        return NULL;
    }

    if (size == 0) {
        return NULL;
    }

    /* Add header size and align to 8 bytes */
    size = (size + BLOCK_HEADER_SIZE + 7) & ~7;

    /* Ensure minimum block size */
    if (size < MIN_BLOCK_SIZE) {
        size = MIN_BLOCK_SIZE;
    }

    /* Find free block using first-fit */
    block = find_free_block(size);
    if (block == NULL) {
        klog_warn("kmalloc: Out of heap memory (requested %u bytes)", (uint32_t)size);
        return NULL;
    }

    /* Split block if it's much larger than needed */
    if (block->size >= size + MIN_BLOCK_SIZE) {
        split_block(block, size);
    }

    /* Mark block as used */
    block->is_free = false;
    heap.num_allocs++;

    /* Return pointer after header */
    ptr = (void *)((uint64_t)block + BLOCK_HEADER_SIZE);
    return ptr;
}

/**
 * Allocate and zero-initialize memory
 */
void *kcalloc(size_t nmemb, size_t size)
{
    size_t total_size = nmemb * size;
    void *ptr;
    uint8_t *byte_ptr;
    size_t i;

    ptr = kmalloc(total_size);
    if (ptr == NULL) {
        return NULL;
    }

    /* Zero-initialize */
    byte_ptr = (uint8_t *)ptr;
    for (i = 0; i < total_size; i++) {
        byte_ptr[i] = 0;
    }

    return ptr;
}

/**
 * Free memory allocated by kmalloc
 */
void kfree(void *ptr)
{
    heap_block_t *block;

    if (ptr == NULL) {
        return;
    }

    if (!heap.initialized) {
        klog_error("Heap not initialized");
        return;
    }

    /* Get block header */
    block = (heap_block_t *)((uint64_t)ptr - BLOCK_HEADER_SIZE);

    /* Validate block is within heap */
    if ((void *)block < heap.heap_start || (void *)block >= heap.heap_end) {
        klog_error("kfree: Invalid pointer %p", ptr);
        return;
    }

    /* Check if already free (double-free detection) */
    if (block->is_free) {
        klog_error("kfree: Double free detected at %p", ptr);
        return;
    }

    /* Mark as free */
    block->is_free = true;
    heap.num_frees++;

    /* Merge adjacent free blocks */
    merge_free_blocks();
}

/**
 * Resize allocated memory block
 */
void *krealloc(void *ptr, size_t new_size)
{
    heap_block_t *block;
    void *new_ptr;
    size_t copy_size;
    uint8_t *src, *dst;
    size_t i;

    /* If ptr is NULL, behave like kmalloc */
    if (ptr == NULL) {
        return kmalloc(new_size);
    }

    /* If new_size is 0, behave like kfree */
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }

    /* Get current block */
    block = (heap_block_t *)((uint64_t)ptr - BLOCK_HEADER_SIZE);

    /* If block is large enough, just return it */
    if (block->size >= new_size + BLOCK_HEADER_SIZE) {
        return ptr;
    }

    /* Allocate new block */
    new_ptr = kmalloc(new_size);
    if (new_ptr == NULL) {
        return NULL;
    }

    /* Copy data from old to new */
    copy_size = block->size - BLOCK_HEADER_SIZE;
    if (new_size < copy_size) {
        copy_size = new_size;
    }

    src = (uint8_t *)ptr;
    dst = (uint8_t *)new_ptr;
    for (i = 0; i < copy_size; i++) {
        dst[i] = src[i];
    }

    /* Free old block */
    kfree(ptr);

    return new_ptr;
}

/**
 * Get heap statistics
 */
void heap_get_stats(heap_stats_t *stats)
{
    heap_block_t *block;
    size_t used_size = 0;
    size_t free_size = 0;
    size_t num_blocks = 0;

    if (stats == NULL || !heap.initialized) {
        return;
    }

    /* Walk block list */
    block = heap.first_block;
    while (block != NULL) {
        num_blocks++;
        if (block->is_free) {
            free_size += block->size;
        } else {
            used_size += block->size;
        }
        block = block->next;
    }

    stats->total_size = heap.heap_size;
    stats->used_size = used_size;
    stats->free_size = free_size;
    stats->num_blocks = num_blocks;
    stats->num_allocs = heap.num_allocs;
    stats->num_frees = heap.num_frees;
}

/**
 * Print heap state for debugging
 */
void heap_dump_state(void)
{
    heap_stats_t stats;
    heap_block_t *block;
    uint32_t block_num = 0;

    kprintf("\n=== Heap State ===\n");
    kprintf("Heap: %p - %p\n", heap.heap_start, heap.heap_end);

    heap_get_stats(&stats);
    kprintf("Total: %u KB\n", (uint32_t)(stats.total_size / 1024));
    kprintf("Used: %u KB (%u%%)\n",
            (uint32_t)(stats.used_size / 1024),
            (uint32_t)(stats.used_size * 100 / stats.total_size));
    kprintf("Free: %u KB (%u%%)\n",
            (uint32_t)(stats.free_size / 1024),
            (uint32_t)(stats.free_size * 100 / stats.total_size));
    kprintf("Blocks: %u\n", (uint32_t)stats.num_blocks);
    kprintf("Allocs: %u, Frees: %u\n",
            (uint32_t)stats.num_allocs,
            (uint32_t)stats.num_frees);

    kprintf("\nBlock list:\n");
    block = heap.first_block;
    while (block != NULL && block_num < 20) {
        kprintf("  [%2u] %p: %6u bytes %s\n",
                block_num,
                block,
                (uint32_t)block->size,
                block->is_free ? "(free)" : "(used)");
        block = block->next;
        block_num++;
    }
    if (block != NULL) {
        kprintf("  ... (more blocks)\n");
    }
    kprintf("=================\n\n");
}

/* ============================================================================
 * Helper functions
 * ============================================================================ */

/**
 * Find first free block that fits (first-fit algorithm)
 */
static heap_block_t *find_free_block(size_t size)
{
    heap_block_t *block = heap.first_block;

    while (block != NULL) {
        if (block->is_free && block->size >= size) {
            return block;
        }
        block = block->next;
    }

    return NULL;
}

/**
 * Split block into two if large enough
 */
static void split_block(heap_block_t *block, size_t size)
{
    heap_block_t *new_block;
    size_t remaining_size;

    remaining_size = block->size - size;

    /* Create new block in remaining space */
    new_block = (heap_block_t *)((uint64_t)block + size);
    new_block->size = remaining_size;
    new_block->is_free = true;
    new_block->next = block->next;
    new_block->prev = block;

    /* Update current block */
    block->size = size;
    block->next = new_block;

    /* Update next block's prev pointer */
    if (new_block->next != NULL) {
        new_block->next->prev = new_block;
    }
}

/**
 * Merge adjacent free blocks
 */
static void merge_free_blocks(void)
{
    heap_block_t *block = heap.first_block;

    while (block != NULL && block->next != NULL) {
        if (block->is_free && block->next->is_free) {
            /* Merge with next block */
            block->size += block->next->size;
            block->next = block->next->next;

            if (block->next != NULL) {
                block->next->prev = block;
            }
        } else {
            block = block->next;
        }
    }
}

/* ============================================================================
 * End of heap.c
 * ============================================================================ */
