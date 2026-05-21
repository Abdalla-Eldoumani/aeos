/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/proc/elf.c
 * Description: elf_validate - the reject-never-fault static ELF64 header gate
 *              (criterion 1). It runs eight ordered checks over the in-kernel
 *              file copy in [buf, buf+size) and returns a distinct negative code
 *              on the first failure, 0 on success. It reads only the fixed
 *              Elf64_Ehdr fields, all behind the size>=sizeof(Elf64_Ehdr) gate,
 *              and computes the program-header table extent in uint64_t so a
 *              crafted e_phoff/e_phnum cannot overflow or push a read past the
 *              buffer. Per-segment bounds (p_offset/p_filesz/p_vaddr) belong to
 *              the loader, where the segments are actually mapped - not here.
 * ============================================================================ */

#include <aeos/elf.h>
#include <aeos/kprintf.h>
#include <aeos/types.h>

/**
 * Validate the ELF64 header in [buf, buf+size) as a loadable static AArch64
 * ET_EXEC. Returns 0 on success, a distinct negative code on the first failed
 * check (the codes aid debugging and feed the criterion-1 reject diagnostics).
 * Never dereferences outside [buf, buf+size): the size gate precedes every
 * field read and there is no program-header access in this function at all.
 */
int elf_validate(const unsigned char *buf, uint64_t size)
{
    /* 1. File big enough for an Ehdr. Checked before reading any field so a
     *    short buffer never gets dereferenced past its end. */
    if (size < sizeof(Elf64_Ehdr)) {
        klog_error("elf_validate: too small (%llu < %llu)",
                   (unsigned long long)size,
                   (unsigned long long)sizeof(Elf64_Ehdr));
        return -1;
    }

    const Elf64_Ehdr *e = (const Elf64_Ehdr *)buf;

    /* 2. Magic 0x7F 'E' 'L' 'F'. */
    if (e->e_ident[EI_MAG0] != ELFMAG0 || e->e_ident[EI_MAG1] != ELFMAG1 ||
        e->e_ident[EI_MAG2] != ELFMAG2 || e->e_ident[EI_MAG3] != ELFMAG3) {
        klog_error("elf_validate: bad magic");
        return -2;
    }

    /* 3. Class: 64-bit only. */
    if (e->e_ident[EI_CLASS] != ELFCLASS64) {
        klog_error("elf_validate: not ELFCLASS64 (e_ident[EI_CLASS]=%u)",
                   (unsigned)e->e_ident[EI_CLASS]);
        return -3;
    }

    /* 4. Data: little-endian only (the kernel is LE; reject big-endian). */
    if (e->e_ident[EI_DATA] != ELFDATA2LSB) {
        klog_error("elf_validate: not ELFDATA2LSB (e_ident[EI_DATA]=%u)",
                   (unsigned)e->e_ident[EI_DATA]);
        return -4;
    }

    /* 5. Type: ET_EXEC only. This is where ET_DYN/PIE is refused - no
     *    relocation handling exists (locked PROJECT decision), so only an
     *    absolute-addressed executable is loadable. */
    if (e->e_type != ET_EXEC) {
        klog_error("elf_validate: not ET_EXEC (e_type=%u)", (unsigned)e->e_type);
        return -5;
    }

    /* 6. Machine: AArch64 only. */
    if (e->e_machine != EM_AARCH64) {
        klog_error("elf_validate: not EM_AARCH64 (e_machine=%u)",
                   (unsigned)e->e_machine);
        return -6;
    }

    /* 7. Program-header table sanity. */
    if (e->e_phentsize != sizeof(Elf64_Phdr)) {
        klog_error("elf_validate: bad e_phentsize (%u, want %llu)",
                   (unsigned)e->e_phentsize,
                   (unsigned long long)sizeof(Elf64_Phdr));
        return -7;
    }
    if (e->e_phnum == 0 || e->e_phnum > 64) {
        klog_error("elf_validate: bad e_phnum (%u)", (unsigned)e->e_phnum);
        return -8;
    }
    if (e->e_phoff > size) {
        klog_error("elf_validate: e_phoff past buffer (%llu > %llu)",
                   (unsigned long long)e->e_phoff,
                   (unsigned long long)size);
        return -9;
    }
    /* Compute the table extent in uint64_t so e_phnum*e_phentsize cannot
     * overflow; the e_phoff<=size check above also guards the addition. */
    if (e->e_phoff + (uint64_t)e->e_phnum * e->e_phentsize > size) {
        klog_error("elf_validate: phdr table runs past buffer "
                   "(e_phoff=%llu e_phnum=%u > %llu)",
                   (unsigned long long)e->e_phoff,
                   (unsigned)e->e_phnum,
                   (unsigned long long)size);
        return -10;
    }

    return 0;
}
