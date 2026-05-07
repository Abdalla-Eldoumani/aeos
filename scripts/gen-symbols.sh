#!/usr/bin/env bash
# ============================================================================
# AEOS - Abdalla's Educational Operating System
# File: scripts/gen-symbols.sh
# Description: Emit a sorted (addr, name) symbol table from a linked kernel
#              ELF for use by the in-kernel backtrace.
#
# Usage: scripts/gen-symbols.sh <input.elf> <output.c>
#
# The output is a freestanding C source that defines:
#   const symbol_entry_t aeos_symbols[];
#   const uint32_t       aeos_symbols_count;
# matching the declarations in include/aeos/symbols.h.
#
# Symbols are sorted by address and limited to text-section entries (T/t/W),
# which is what symbol_lookup binary-searches.
# ============================================================================

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <input.elf> <output.c>" >&2
    exit 1
fi

INPUT=$1
OUTPUT=$2
NM=${NM:-aarch64-linux-gnu-nm}

if [[ ! -f $INPUT ]]; then
    echo "$0: input ELF '$INPUT' not found" >&2
    exit 1
fi

if ! command -v "$NM" >/dev/null 2>&1; then
    echo "$0: '$NM' not on PATH (set NM= to override)" >&2
    exit 1
fi

mkdir -p "$(dirname "$OUTPUT")"

# nm -n prints symbols sorted by numeric address. We keep T (global text),
# t (local text), and W (weak) entries; everything else is data, debug, or
# linker-internal noise. Mapping symbols emitted by the assembler ($x, $d)
# would clutter the table with addresses that don't correspond to functions,
# so they are filtered out.
TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT

"$NM" -n "$INPUT" \
    | awk '($2 == "T" || $2 == "t" || $2 == "W") && substr($3, 1, 1) != "$" {
        print $1, $3
    }' > "$TMP"

COUNT=$(wc -l < "$TMP" | tr -d ' ')

{
    echo '/* ============================================================================'
    echo ' * AEOS - Abdalla'\''s Educational Operating System'
    echo " * File: $OUTPUT"
    echo " * Description: Auto-generated symbol table for in-kernel backtrace."
    echo " *              Generated from $INPUT by scripts/gen-symbols.sh."
    echo ' *              DO NOT EDIT BY HAND - this file is regenerated on every build.'
    echo ' * ============================================================================ */'
    echo
    echo '#include <aeos/symbols.h>'
    echo
    echo 'const symbol_entry_t aeos_symbols[] = {'
    while read -r addr name; do
        # Strip any stray double-quote characters from the name to keep the
        # generated string literal well-formed. C identifiers cannot contain
        # quotes, so this is a no-op in practice; the guard is here for
        # robustness against weird linker scripts.
        printf '    { 0x%sULL, "%s" },\n' "$addr" "${name//\"/}"
    done < "$TMP"
    echo '};'
    echo
    echo "const uint32_t aeos_symbols_count = ${COUNT}u;"
} > "$OUTPUT"
