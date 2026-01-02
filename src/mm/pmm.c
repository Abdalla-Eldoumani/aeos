/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/mm/pmm.c
 * Description: Physical Memory Manager - Buddy Allocator Implementation
 * ============================================================================ */

#include <aeos/pmm.h>
#include <aeos/mm.h>
#include <aeos/types.h>
#include <aeos/kprintf.h>

/**
 * Free list node for buddy allocator
 * Each free block has a node at its start
 */
typedef struct free_block {
    struct free_block *next;
} free_block_t;

/**
 * Buddy allocator state
 */
static struct {
    free_block_t *free_lists[PMM_MAX_ORDER + 1];  /* Free lists for each order */
    uint64_t mem_start;                            /* Start of managed memory */
    uint64_t mem_end;                              /* End of managed memory */
    size_t total_pages;                            /* Total number of pages */
    size_t free_pages;                             /* Number of free pages */
    bool initialized;                              /* Initialization flag */
} pmm;

/* Forward declarations */
static uint64_t get_buddy_addr(uint64_t addr, uint32_t order);
static void add_to_free_list(uint64_t addr, uint32_t order);
static uint64_t remove_from_free_list(uint32_t order);
static bool is_in_free_list(uint64_t addr, uint32_t order);

/**
 * Initialize the Physical Memory Manager
 */
void pmm_init(uint64_t mem_start, uint64_t mem_end, uint64_t kernel_end)
{
    uint32_t i;
    uint64_t available_start;
    uint64_t available_end;
    uint64_t current;

    klog_info("Initializing Physical Memory Manager...");

    /* Initialize free lists */
    for (i = 0; i <= PMM_MAX_ORDER; i++) {
        pmm.free_lists[i] = NULL;
    }

    /* Align memory boundaries to page size */
    mem_start = PAGE_ALIGN_UP(mem_start);
    mem_end = PAGE_ALIGN_DOWN(mem_end);
    kernel_end = PAGE_ALIGN_UP(kernel_end);

    /* Save memory bounds */
    pmm.mem_start = mem_start;
    pmm.mem_end = mem_end;
    pmm.total_pages = (mem_end - mem_start) >> PAGE_SHIFT;
    pmm.free_pages = 0;

    /* Available memory starts after kernel */
    available_start = kernel_end;
    available_end = mem_end;

    kprintf("  Memory range: %p - %p\n", (void *)mem_start, (void *)mem_end);
    kprintf("  Kernel ends at: %p\n", (void *)kernel_end);
    kprintf("  Available: %p - %p\n", (void *)available_start, (void *)available_end);
    kprintf("  Total pages: %u (%u MB)\n", (uint32_t)pmm.total_pages, (uint32_t)(pmm.total_pages * PAGE_SIZE / (1024 * 1024)));

    /*
     * Add all available memory to free lists
     * Start with largest possible blocks and work down
     */
    current = available_start;
    while (current < available_end) {
        uint64_t remaining = available_end - current;
        int order;  /* Use signed int to avoid comparison issues */
        uint64_t block_size;
        bool found = false;

        /* Find largest block that fits */
        for (order = PMM_MAX_ORDER; order >= 0; order--) {
            block_size = PAGE_SIZE << order;

            /* Check if block fits and is properly aligned */
            if (remaining >= block_size &&
                (current & (block_size - 1)) == 0) {
                /* Add this block to free list */
                add_to_free_list(current, (uint32_t)order);
                current += block_size;
                pmm.free_pages += (1 << order);
                found = true;
                break;
            }
        }

        /* If no block fit, move to next page */
        if (!found) {
            current += PAGE_SIZE;
        }
    }

    pmm.initialized = true;

    kprintf("  Free pages: %u (%u MB)\n",
            (uint32_t)pmm.free_pages,
            (uint32_t)(pmm.free_pages * PAGE_SIZE / (1024 * 1024)));
    klog_info("PMM initialization complete");
}

/**
 * Allocate 2^order contiguous physical pages
 */
uint64_t pmm_alloc_pages(uint32_t order)
{
    uint32_t current_order;
    uint64_t addr;

    if (!pmm.initialized) {
        klog_error("PMM not initialized");
        return 0;
    }

    if (order > PMM_MAX_ORDER) {
        klog_error("Allocation order %u exceeds maximum %u", order, PMM_MAX_ORDER);
        return 0;
    }

    /* Find smallest available block that fits */
    for (current_order = order; current_order <= PMM_MAX_ORDER; current_order++) {
        if (pmm.free_lists[current_order] != NULL) {
            /* Found a block, remove it from free list */
            addr = remove_from_free_list(current_order);

            /* Split block if it's larger than needed */
            while (current_order > order) {
                current_order--;
                /* Add buddy (second half) to free list */
                add_to_free_list(addr + (PAGE_SIZE << current_order), current_order);
            }

            /* Update statistics */
            pmm.free_pages -= (1 << order);

            return addr;
        }
    }

    /* No memory available */
    klog_warn("PMM: Out of memory (order %u)", order);
    return 0;
}

/**
 * Free 2^order contiguous physical pages
 */
void pmm_free_pages(uint64_t addr, uint32_t order)
{
    uint64_t buddy_addr;

    if (!pmm.initialized) {
        klog_error("PMM not initialized");
        return;
    }

    if (order > PMM_MAX_ORDER) {
        klog_error("Free order %u exceeds maximum %u", order, PMM_MAX_ORDER);
        return;
    }

    /* Check if address is valid and aligned */
    if (addr < pmm.mem_start || addr >= pmm.mem_end) {
        klog_error("PMM: Invalid address %p", (void *)addr);
        return;
    }

    if (addr & ((PAGE_SIZE << order) - 1)) {
        klog_error("PMM: Address %p not aligned for order %u", (void *)addr, order);
        return;
    }

    /* Try to merge with buddy */
    while (order < PMM_MAX_ORDER) {
        buddy_addr = get_buddy_addr(addr, order);

        /* Check if buddy is free */
        if (buddy_addr < pmm.mem_start || buddy_addr >= pmm.mem_end ||
            !is_in_free_list(buddy_addr, order)) {
            break;  /* Buddy not free, stop merging */
        }

        /* Remove buddy from free list */
        remove_from_free_list(order);

        /* Merge with buddy (use lower address) */
        if (buddy_addr < addr) {
            addr = buddy_addr;
        }

        /* Move up to next order */
        order++;
    }

    /* Add merged block to free list */
    add_to_free_list(addr, order);

    /* Update statistics */
    pmm.free_pages += (1 << order);
}

/**
 * Reserve a range of physical memory
 */
void pmm_reserve_region(uint64_t start, uint64_t end)
{
    uint64_t current;
    int order;  /* Use signed int to avoid comparison issues */

    start = PAGE_ALIGN_DOWN(start);
    end = PAGE_ALIGN_UP(end);

    klog_debug("Reserving region: %p - %p", (void *)start, (void *)end);

    /* Remove pages from free lists */
    for (current = start; current < end; ) {
        bool found = false;

        /* Find largest block starting at current address */
        for (order = PMM_MAX_ORDER; order >= 0; order--) {
            uint64_t block_size = PAGE_SIZE << order;

            if (current + block_size <= end &&
                (current & (block_size - 1)) == 0 &&
                is_in_free_list(current, (uint32_t)order)) {
                /* Remove this block */
                remove_from_free_list((uint32_t)order);
                pmm.free_pages -= (1 << order);
                current += block_size;
                found = true;
                break;
            }
        }

        if (!found) {
            current += PAGE_SIZE;
        }
    }
}

/**
 * Get physical memory statistics
 */
void pmm_get_stats(pmm_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }

    stats->total_pages = pmm.total_pages;
    stats->free_pages = pmm.free_pages;
    stats->used_pages = pmm.total_pages - pmm.free_pages;
    stats->reserved_pages = 0;  /* TODO: track reserved pages */
}

/**
 * Print PMM state for debugging
 */
void pmm_dump_state(void)
{
    uint32_t order;
    uint32_t count;
    free_block_t *block;

    kprintf("\n=== PMM State ===\n");
    kprintf("Memory: %p - %p\n", (void *)pmm.mem_start, (void *)pmm.mem_end);
    kprintf("Total pages: %u\n", (uint32_t)pmm.total_pages);
    kprintf("Free pages: %u\n", (uint32_t)pmm.free_pages);
    kprintf("Used pages: %u\n", (uint32_t)(pmm.total_pages - pmm.free_pages));

    kprintf("\nFree lists:\n");
    for (order = 0; order <= PMM_MAX_ORDER; order++) {
        count = 0;
        block = pmm.free_lists[order];
        while (block != NULL) {
            count++;
            block = block->next;
        }
        if (count > 0) {
            kprintf("  Order %2u (%5u KB): %u blocks\n",
                    order,
                    (uint32_t)((PAGE_SIZE << order) / 1024),
                    count);
        }
    }
    kprintf("================\n\n");
}

/* ============================================================================
 * Helper functions
 * ============================================================================ */

/**
 * Calculate buddy address for a given block
 */
static uint64_t get_buddy_addr(uint64_t addr, uint32_t order)
{
    uint64_t block_size = PAGE_SIZE << order;
    return addr ^ block_size;
}

/**
 * Add block to free list
 */
static void add_to_free_list(uint64_t addr, uint32_t order)
{
    free_block_t *block = (free_block_t *)addr;
    block->next = pmm.free_lists[order];
    pmm.free_lists[order] = block;
}

/**
 * Remove first block from free list
 */
static uint64_t remove_from_free_list(uint32_t order)
{
    free_block_t *block = pmm.free_lists[order];
    if (block == NULL) {
        return 0;
    }

    pmm.free_lists[order] = block->next;
    return (uint64_t)block;
}

/**
 * Check if address is in free list
 */
static bool is_in_free_list(uint64_t addr, uint32_t order)
{
    free_block_t *block = pmm.free_lists[order];

    while (block != NULL) {
        if ((uint64_t)block == addr) {
            return true;
        }
        block = block->next;
    }

    return false;
}

/* ============================================================================
 * End of pmm.c
 * ============================================================================ */
