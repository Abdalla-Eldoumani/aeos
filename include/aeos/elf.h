/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/elf.h
 * Description: Minimal static ELF64 for AArch64 - the byte-exact Elf64_Ehdr /
 *              Elf64_Phdr layouts, the ELF magic/class/data/type/machine and
 *              PT_LOAD/PF_* constants, and the elf_validate header gate. No libc
 *              types: the integer widths come from <aeos/types.h>. Layouts were
 *              verified byte-for-byte against a real aarch64-linux-gnu-gcc
 *              -nostdlib -static build (06-RESEARCH.md Q2). Static ELF64 only -
 *              dynamic linking / relocations are out of scope (locked PROJECT
 *              decision), which is why elf_validate rejects ET_DYN/PIE.
 * ============================================================================ */

#ifndef AEOS_ELF_H
#define AEOS_ELF_H

#include <aeos/types.h>

/* ELF64 base types. The widths make the structs below pack exactly by natural
 * alignment (no explicit packing): every field is 8/4/2 bytes and laid out so
 * sizeof(Elf64_Ehdr)==64 and sizeof(Elf64_Phdr)==56. */
typedef uint64_t Elf64_Addr;    /* 8 */
typedef uint64_t Elf64_Off;     /* 8 */
typedef uint16_t Elf64_Half;    /* 2 */
typedef uint32_t Elf64_Word;    /* 4 */
typedef uint64_t Elf64_Xword;   /* 8 */

#define EI_NIDENT 16

typedef struct {                       /* offset */
    unsigned char e_ident[EI_NIDENT];  /* 0x00: 16 bytes */
    Elf64_Half    e_type;              /* 0x10 */
    Elf64_Half    e_machine;           /* 0x12 */
    Elf64_Word    e_version;           /* 0x14 */
    Elf64_Addr    e_entry;             /* 0x18 */
    Elf64_Off     e_phoff;             /* 0x20 */
    Elf64_Off     e_shoff;             /* 0x28 */
    Elf64_Word    e_flags;             /* 0x30 */
    Elf64_Half    e_ehsize;            /* 0x34 */
    Elf64_Half    e_phentsize;         /* 0x36 */
    Elf64_Half    e_phnum;             /* 0x38 */
    Elf64_Half    e_shentsize;         /* 0x3A */
    Elf64_Half    e_shnum;             /* 0x3C */
    Elf64_Half    e_shstrndx;          /* 0x3E */
} Elf64_Ehdr;                          /* sizeof = 64 (0x40) */

typedef struct {                       /* offset */
    Elf64_Word    p_type;              /* 0x00 */
    Elf64_Word    p_flags;             /* 0x04  (Elf64 puts p_flags right after
                                        *        p_type, unlike Elf32 where it is
                                        *        the last field) */
    Elf64_Off     p_offset;            /* 0x08 */
    Elf64_Addr    p_vaddr;             /* 0x10 */
    Elf64_Addr    p_paddr;             /* 0x18 */
    Elf64_Xword   p_filesz;            /* 0x20 */
    Elf64_Xword   p_memsz;             /* 0x28 */
    Elf64_Xword   p_align;             /* 0x30 */
} Elf64_Phdr;                          /* sizeof = 56 (0x38) */

/* These guard against an accidental field or type edit silently changing the
 * on-disk layout the validator and loader depend on. _Static_assert is a C11
 * construct gcc accepts in the freestanding C dialect used here. */
_Static_assert(sizeof(Elf64_Ehdr) == 64, "Elf64_Ehdr must be 64 bytes");
_Static_assert(sizeof(Elf64_Phdr) == 56, "Elf64_Phdr must be 56 bytes");

/* e_ident indices */
#define EI_MAG0    0
#define EI_MAG1    1
#define EI_MAG2    2
#define EI_MAG3    3
#define EI_CLASS   4
#define EI_DATA    5
#define EI_VERSION 6

/* e_ident magic + class/data/version */
#define ELFMAG0     0x7F
#define ELFMAG1     'E'
#define ELFMAG2     'L'
#define ELFMAG3     'F'
#define ELFCLASS64  2
#define ELFDATA2LSB 1
#define EV_CURRENT  1

/* e_type / e_machine */
#define ET_EXEC    2
#define ET_DYN     3
#define EM_AARCH64 183   /* 0xB7 */

/* p_type / p_flags */
#define PT_LOAD 1
#define PF_X    0x1
#define PF_W    0x2
#define PF_R    0x4

/**
 * Validate the ELF64 header in [buf, buf+size) as a loadable static AArch64
 * executable. Returns 0 iff buf is a little-endian AArch64 ET_EXEC ELF64 whose
 * program-header table lies wholly inside [buf, buf+size); returns a negative
 * error code (and logs the reason) otherwise. This is the criterion-1 gate:
 * a malformed or non-ELF file produces a clean error, never a fault - the
 * function never dereferences any address outside [buf, buf+size).
 *
 * ET_DYN/PIE is rejected at the e_type check: this kernel applies no
 * relocations (locked PROJECT decision), so only an absolute-addressed ET_EXEC
 * is loadable. Per-segment bounds (p_offset/p_filesz/p_vaddr) are the loader's
 * job, not this header gate.
 */
int elf_validate(const unsigned char *buf, uint64_t size);

#endif /* AEOS_ELF_H */
