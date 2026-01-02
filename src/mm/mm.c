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

    /* Initialize Physical Memory Manager - start allocating AFTER the heap */
    pmm_init(PHYS_RAM_START, PHYS_RAM_END, heap_end);

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
