/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/lines.h
 * Description: In-kernel addr->file:line table and lookup API. The sibling of
 *              symbols.h: where aeos_symbols maps an address to a function
 *              name, aeos_lines maps it to a source file and line.
 * ============================================================================ */

#ifndef AEOS_LINES_H
#define AEOS_LINES_H

#include <aeos/types.h>

/**
 * One entry in the kernel line table. The table is sorted by `addr_off` and
 * generated from the linked ELF's DWARF line program by scripts/gen-lines.py,
 * then linked into the kernel on a second pass so it can describe its own
 * source locations. The 8-byte packed layout keeps the ~137 KB table small:
 * `addr_off` is the PC minus the kernel base (linker.ld ORIGIN, 0x40000000),
 * `file_off` is a byte offset into `aeos_line_files`, and `line` is the source
 * line. The max line (~1768) and the file count (~57) both fit a uint16_t.
 */
typedef struct {
    uint32_t addr_off;   /* PC - 0x40000000 (the kernel base) */
    uint16_t file_off;   /* byte offset into aeos_line_files */
    uint16_t line;       /* source line number */
} aeos_line_entry_t;

extern const aeos_line_entry_t aeos_lines[];
extern const uint32_t aeos_lines_count;
extern const char aeos_line_files[];   /* NUL-separated file-name pool */

/**
 * Resolve an address to "<file>:<line>" via binary search over the line table,
 * keyed on `addr - 0x40000000`. Always writes a NUL-terminated string; writes
 * an empty string when the table is empty or the address is below the first
 * known entry.
 *
 * Declared here; implemented in backtrace.c (the per-frame "(file:line)" print
 * is added alongside the existing symbol lookup).
 */
void line_lookup(uint64_t addr, char *buf, uint32_t buf_size);

#endif /* AEOS_LINES_H */
