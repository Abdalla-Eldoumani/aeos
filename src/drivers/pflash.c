/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/drivers/pflash.c
 * Description: Parallel flash (pflash) persistence - simple memory-mapped I/O
 * ============================================================================ */

#include <aeos/pflash.h>
#include <aeos/kprintf.h>
#include <aeos/string.h>

static int pflash_initialized = 0;

/**
 * Initialize pflash persistence
 */
void pflash_init(void)
{
    kprintf("  [INFO] Pflash persistence: memory-mapped at 0x%x (size: %u MB)\n",
            PFLASH_BASE, PFLASH_SIZE / (1024 * 1024));
    pflash_initialized = 1;
}

/**
 * Write data to pflash (just a memcpy to the mapped address!)
 */
int pflash_write(uint32_t offset, const void *data, size_t size)
{
    if (!pflash_initialized) {
        klog_error("Pflash not initialized");
        return -1;
    }

    if (offset + size > PFLASH_SIZE) {
        klog_error("Pflash write beyond bounds: offset=%u size=%u", offset, (uint32_t)size);
        return -1;
    }

    void *dest = (void *)(uintptr_t)(PFLASH_BASE + offset);
    memcpy(dest, data, size);

    kprintf("  [DEBUG] Pflash: wrote %u bytes to offset %u\n", (uint32_t)size, offset);
    return 0;
}

/**
 * Read data from pflash (just a memcpy from the mapped address!)
 */
int pflash_read(uint32_t offset, void *data, size_t size)
{
    if (!pflash_initialized) {
        klog_error("Pflash not initialized");
        return -1;
    }

    if (offset + size > PFLASH_SIZE) {
        klog_error("Pflash read beyond bounds: offset=%u size=%u", offset, (uint32_t)size);
        return -1;
    }

    void *src = (void *)(uintptr_t)(PFLASH_BASE + offset);
    memcpy(data, src, size);

    kprintf("  [DEBUG] Pflash: read %u bytes from offset %u\n", (uint32_t)size, offset);
    return 0;
}

/* ============================================================================
 * End of pflash.c
 * ============================================================================ */
