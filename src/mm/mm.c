/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/mm/mm.c
 * Description: Memory management subsystem initialization
 * ============================================================================ */

#include <aeos/mm.h>
#include <aeos/pmm.h>
#include <aeos/heap.h>
#include <aeos/kprintf.h>

/* External symbols from linker script */
extern char _kernel_end;
extern char __heap_start;
extern char __heap_end;
extern char __stack_top;

/**
 * Initialize all memory management subsystems
 */
void mm_init(void)
{
    uint64_t heap_start;
    uint64_t heap_end;
    uint64_t heap_size;

    klog_info("Initializing Memory Management subsystem");
    kprintf("\n");

    /* Get heap boundaries */
    heap_start = (uint64_t)&__heap_start;
    heap_end = (uint64_t)&__heap_end;
    heap_size = heap_end - heap_start;

    /* Start PMM allocations above the boot stack, not at heap_end. The .stack
     * region [heap_end, __stack_top) holds the live boot stack and the stack
     * guard sentinel at __stack_limit (== heap_end). The PMM writes a free-list
     * node at the start of its first block, so starting at heap_end would
     * clobber the sentinel and corrupt the in-use stack. This matches the
     * documented intent that the PMM covers memory from the stack top up. */
    pmm_init(PHYS_RAM_START, PHYS_RAM_END, (uint64_t)&__stack_top);

    /* Initialize kernel heap */
    heap_init((void *)heap_start, heap_size);

    kprintf("\n");
    klog_info("Memory Management initialization complete");
}

/**
 * Get total available physical memory in bytes
 */
size_t mm_get_total_memory(void)
{
    pmm_stats_t stats;
    pmm_get_stats(&stats);
    return stats.total_pages * PAGE_SIZE;
}

/**
 * Get free physical memory in bytes
 */
size_t mm_get_free_memory(void)
{
    pmm_stats_t stats;
    pmm_get_stats(&stats);
    return stats.free_pages * PAGE_SIZE;
}

/**
 * Get used physical memory in bytes
 */
size_t mm_get_used_memory(void)
{
    pmm_stats_t stats;
    pmm_get_stats(&stats);
    return stats.used_pages * PAGE_SIZE;
}

/* ============================================================================
 * End of mm.c
 * ============================================================================ */
