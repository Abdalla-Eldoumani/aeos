#!/usr/bin/env python3
# ============================================================================
# AEOS - Abdalla's Educational Operating System
# File: scripts/gen-lines.py
# Description: Emit a sorted addr->file:line table from a linked kernel ELF's
#              DWARF line program for use by the in-kernel backtrace.
#
# Usage: python3 scripts/gen-lines.py <input.elf> <output.c>
#
# The output is a freestanding C source that defines:
#   const aeos_line_entry_t aeos_lines[];
#   const uint32_t          aeos_lines_count;
#   const char              aeos_line_files[];
# matching the declarations in include/aeos/lines.h.
#
# The table is the decoded output of the toolchain's own DWARF line program
# (readelf --debug-dump=decodedline), the addr->file:line sibling of the
# addr->name table scripts/gen-symbols.sh emits. It is extracted at build time
# because .debug_line has VMA 0 and is not loaded by `qemu -kernel`, so an
# in-kernel parser would read zeros.
# ============================================================================

import subprocess
import sys
import os

# The kernel base (linker.ld ORIGIN). addr_off is the PC minus this so it fits
# a uint32_t in the packed entry; 09-05's lookup keys on lr - TEXT_BASE.
TEXT_BASE = 0x40000000

# The packed-entry field widths cap the table. A future kernel that exceeds any
# of these must fail the build loudly rather than embed a truncated table.
U16_MAX = 0xFFFF


def die(msg):
    sys.stderr.write("gen-lines.py: " + msg + "\n")
    sys.exit(1)


def main():
    if len(sys.argv) != 3:
        sys.stderr.write("usage: %s <input.elf> <output.c>\n" % sys.argv[0])
        sys.exit(1)

    input_elf = sys.argv[1]
    output_c = sys.argv[2]
    readelf = os.environ.get("READELF", "aarch64-linux-gnu-readelf")

    if not os.path.isfile(input_elf):
        die("input ELF '%s' not found" % input_elf)

    # Decode the DWARF line program. This is the toolchain's own interpreter;
    # we parse its plain-text rows rather than re-implementing the state machine.
    try:
        proc = subprocess.run(
            [readelf, "--debug-dump=decodedline", input_elf],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
        )
    except FileNotFoundError:
        die("'%s' not on PATH (set READELF= to override)" % readelf)
    except subprocess.CalledProcessError as exc:
        sys.stderr.write(exc.stderr.decode("utf-8", "replace"))
        die("'%s --debug-dump=decodedline' failed" % readelf)

    text = proc.stdout.decode("utf-8", "replace")

    # Each data row holds a starting address token matching ^0x[0-9a-fA-F]+$.
    # The line number is the token immediately before it and the file name the
    # token before that; the trailing View/Stmt columns sit after the address,
    # so keying on the address token's position survives them. Rows without an
    # address token (the section banner, the per-file "File name ... Stmt"
    # header, blank separators, and a sequence-end row whose line is a literal
    # "-") carry no addr->file:line mapping and are skipped.
    rows = []  # (address, file, line)
    for raw in text.splitlines():
        tokens = raw.split()
        if len(tokens) < 3:
            continue
        addr_idx = -1
        for i, tok in enumerate(tokens):
            if tok.startswith("0x") and is_hex(tok[2:]):
                addr_idx = i
                break
        # Need a file and a line token before the address.
        if addr_idx < 2:
            continue
        line_tok = tokens[addr_idx - 1]
        file_tok = tokens[addr_idx - 2]
        # A sequence-end row uses "-" for the line; it has no source mapping.
        if not line_tok.isdigit():
            continue
        address = int(tokens[addr_idx], 16)
        line = int(line_tok)
        rows.append((address, file_tok, line))

    if not rows:
        die("no decoded line rows parsed from '%s' (no DWARF line info?)" % input_elf)

    # Sort by address. A stable sort keeps the readelf emission order within a
    # single address, so the first row of a run is the canonical one.
    rows.sort(key=lambda r: r[0])

    # Collapse consecutive same-(file,line) runs, keeping the FIRST entry. The
    # backtrace wants the entry whose address is the largest <= PC, so only the
    # boundary where (file,line) changes needs an entry.
    collapsed = []
    prev = None  # (file, line)
    for address, fname, line in rows:
        cur = (fname, line)
        if cur != prev:
            collapsed.append((address, fname, line))
            prev = cur

    # Build a deduplicated NUL-separated file-name pool and record each file's
    # byte offset. The pool is emitted as a byte array, not a string literal,
    # because embedded NULs in a literal trip -Werror (Pitfall 4).
    pool = bytearray()
    file_off = {}
    for _, fname, _ in collapsed:
        if fname not in file_off:
            file_off[fname] = len(pool)
            pool.extend(fname.encode("utf-8"))
            pool.append(0)

    # GUARD/CLAMP: the packed entry uses u16 for file_off and line, so a kernel
    # that grows past those limits must fail the build loudly. Never silently
    # truncate to a wrong table.
    max_line = max(line for _, _, line in collapsed)
    if max_line > U16_MAX:
        die("max source line %d exceeds u16 (%d); widen aeos_line_entry_t.line"
            % (max_line, U16_MAX))
    if len(pool) > U16_MAX:
        die("file-name pool is %d bytes, exceeds u16 file_off range (%d); "
            "widen aeos_line_entry_t.file_off" % (len(pool), U16_MAX))
    max_off = max(file_off.values()) if file_off else 0
    if max_off > U16_MAX:
        die("file_off %d exceeds u16 (%d); widen aeos_line_entry_t.file_off"
            % (max_off, U16_MAX))
    # The address span fits u32 by construction (text is well under 4 GB above
    # the base), but a row below the base would underflow the offset.
    for address, fname, line in collapsed:
        if address < TEXT_BASE:
            die("address 0x%x is below TEXT_BASE 0x%x (%s:%d); cannot offset"
                % (address, TEXT_BASE, fname, line))
        if address - TEXT_BASE > 0xFFFFFFFF:
            die("addr_off for 0x%x exceeds u32" % address)

    write_output(output_c, input_elf, collapsed, file_off, pool)


def is_hex(s):
    if not s:
        return False
    for ch in s:
        if ch not in "0123456789abcdefABCDEF":
            return False
    return True


def write_output(output_c, input_elf, entries, file_off, pool):
    out_dir = os.path.dirname(output_c)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    lines = []
    lines.append("/* ============================================================================")
    lines.append(" * AEOS - Abdalla's Educational Operating System")
    lines.append(" * File: %s" % output_c)
    lines.append(" * Description: Auto-generated addr->file:line table for in-kernel backtrace.")
    lines.append(" *              Generated from %s by scripts/gen-lines.py." % input_elf)
    lines.append(" *              DO NOT EDIT BY HAND - this file is regenerated on every build.")
    lines.append(" * ============================================================================ */")
    lines.append("")
    lines.append("#include <aeos/lines.h>")
    lines.append("")
    lines.append("const aeos_line_entry_t aeos_lines[] = {")
    for address, fname, line in entries:
        addr_off = address - TEXT_BASE
        lines.append("    { 0x%xu, %uu, %uu }," % (addr_off, file_off[fname], line))
    lines.append("};")
    lines.append("")
    lines.append("const uint32_t aeos_lines_count = %uu;" % len(entries))
    lines.append("")
    # The pool is a byte-array initializer so its NUL separators do not sit in a
    # string literal (which -Werror rejects). Wrap to keep lines readable.
    lines.append("const char aeos_line_files[] = {")
    row = "   "
    for i, b in enumerate(pool):
        row += " %d," % b
        if (i + 1) % 16 == 0:
            lines.append(row)
            row = "   "
    if row.strip():
        lines.append(row)
    lines.append("};")
    lines.append("")

    with open(output_c, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
