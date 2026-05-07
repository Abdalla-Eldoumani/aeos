/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/symbols.h
 * Description: In-kernel symbol table and backtrace API
 * ============================================================================ */

#ifndef AEOS_SYMBOLS_H
#define AEOS_SYMBOLS_H

#include <aeos/types.h>

/**
 * One entry in the kernel symbol table. The table is sorted by `addr` and
 * generated from the linked ELF by scripts/gen-symbols.sh, then linked into
 * the kernel on a second pass so it can describe its own functions.
 */
typedef struct {
    uint64_t addr;
    const char *name;
} symbol_entry_t;

extern const symbol_entry_t aeos_symbols[];
extern const uint32_t aeos_symbols_count;

/**
 * Resolve an address to "<name>+0x<offset>" via binary search over the
 * symbol table. Always writes a NUL-terminated string. Falls back to
 * "<unknown>" when the table is empty or the address is below the first
 * known function.
 */
void symbol_lookup(uint64_t addr, char *name_buf, uint32_t buf_size);

/**
 * Walk the AArch64 frame-pointer chain rooted at `fp` and print each saved
 * link register through symbol_lookup. Stops on a NULL fp, an out-of-range
 * fp, or a fp that does not strictly grow toward higher addresses.
 *
 * Pass the value of x29 captured at the call site (or out of cpu_context_t
 * during exception entry) as `fp`.
 */
void backtrace(uint64_t fp);

#endif /* AEOS_SYMBOLS_H */
