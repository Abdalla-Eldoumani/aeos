/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/pflash.h
 * Description: Parallel flash (pflash) persistence interface
 * ============================================================================ */

#ifndef PFLASH_H
#define PFLASH_H

#include <aeos/types.h>

/* Pflash base address on ARM virt machine (second flash bank) */
#define PFLASH_BASE 0x04000000
#define PFLASH_SIZE (64 * 1024 * 1024)  /* 64MB */

/**
 * Initialize pflash persistence
 */
void pflash_init(void);

/**
 * Write data to pflash
 */
int pflash_write(uint32_t offset, const void *data, size_t size);

/**
 * Read data from pflash
 */
int pflash_read(uint32_t offset, void *data, size_t size);

#endif /* PFLASH_H */

/* ============================================================================
 * End of pflash.h
 * ============================================================================ */
