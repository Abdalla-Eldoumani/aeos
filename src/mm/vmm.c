/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/mm/vmm.c
 * Description: MMU bringup. Builds one level-1 page table with 1GB block
 *              descriptors (identity-map RAM Normal + the low MMIO window
 *              Device-nGnRnE), installs it in TTBR0_EL1 as the running map and
 *              a TTBR1_EL1 high-half alias of RAM, then enables the MMU and
 *              caches. The kernel keeps executing from its physical/low
 *              addresses; TTBR1 is a demonstrated alias, not yet the sole map.
 * ============================================================================ */

#include <aeos/vmm.h>
#include <aeos/mm.h>
#include <aeos/kprintf.h>

/* Linker symbol; the alias check reads the first word of the kernel image.
 * Declared as an unbounded array (not a scalar char) so the 4-byte read in
 * vmm_report does not trip -Werror=array-bounds: a scalar `extern char` has a
 * known size of 1, and GCC then flags the uint32_t load as out of bounds. The
 * symbol marks an address, so its real extent is the whole kernel image. */
extern char _kernel_start[];

/* MAIR_EL1 attribute indices selected by the descriptor AttrIndx field. */
#define ATTRIDX_DEVICE  0u  /* MAIR Attr0 = 0x00 Device-nGnRnE */
#define ATTRIDX_NORMAL  1u  /* MAIR Attr1 = 0xFF Normal WB inner+outer RW-alloc */

/* L1 block descriptor lower-attribute bit positions (ARMv8-A VMSAv8-64, D4.3). */
#define DESC_TYPE_BLOCK 0x1ULL          /* bits[1:0]=0b01: block at L1 (terminates) */
#define DESC_AF         (1ULL << 10)    /* Access Flag; clear faults on first touch */
#define DESC_SH_INNER   (3ULL << 8)     /* SH[1:0]=0b11 Inner Shareable */
#define DESC_PXN        (1ULL << 53)    /* Privileged Execute Never */
#define DESC_UXN        (1ULL << 54)    /* Unprivileged Execute Never */

/* 1GB-aligned output-address mask for an L1 block (bits [47:30]). */
#define L1_OA_MASK      0x0000FFFFC0000000ULL

/* L1 entry index for a VA is (VA >> 30) & 0x1FF (1GB per L1 entry). */
#define L1_INDEX(va)    (((va) >> 30) & 0x1FFULL)

/* SCTLR_EL1 enable bits. Set M, C, I together in a single write: caching with
 * the MMU off is implementation-defined, so C must never be set without M. */
#define SCTLR_M  (1ULL << 0)
#define SCTLR_C  (1ULL << 2)
#define SCTLR_I  (1ULL << 12)

/* The two L1 tables live in BSS (zeroed by boot.asm), so only the handful of
 * live entries are written. They must NOT be memset: -mgeneral-regs-only would
 * trap a vectorized clear, and the unused entries already read as 0 (invalid). */
static uint64_t ttbr0_l1[512] __attribute__((aligned(4096)));  /* running identity map */
static uint64_t ttbr1_l1[512] __attribute__((aligned(4096)));  /* high-half kernel alias */

/**
 * Build an 8-byte L1 block descriptor: 1GB-aligned output address, AF set,
 * the given AttrIndx and shareability, AP=00 (EL1 RW, EL0 no-access), block type.
 */
static inline uint64_t l1_block(uint64_t pa, uint32_t attridx, uint64_t sh)
{
    return (pa & L1_OA_MASK)
         | DESC_AF
         | sh
         | (0ULL << 6)                 /* AP[2:1]=00: EL1 read/write */
         | ((uint64_t)attridx << 2)    /* AttrIndx selects the MAIR byte */
         | DESC_TYPE_BLOCK;
}

/**
 * Fill the identity table and the TTBR1 alias. RAM is 256MB so all of it sits
 * inside the single 1GB Normal block at 0x40000000; one entry covers the
 * kernel, heap, framebuffer, stack, and virtqueues.
 */
static void vmm_build_tables(void)
{
    /* 0x00000000-0x3FFFFFFF: all MMIO (GIC 0x08.., UART 0x09.., virtio 0x0a..)
     * as Device-nGnRnE, marked never-execute (the kernel never runs from MMIO). */
    ttbr0_l1[L1_INDEX(0x00000000ULL)] =
        l1_block(0x00000000ULL, ATTRIDX_DEVICE, 0 /* SH ignored for Device */)
        | DESC_PXN | DESC_UXN;

    /* 0x40000000-0x7FFFFFFF: RAM as Normal WB Inner-Shareable. The kernel
     * executes from here, so PXN/UXN stay clear. */
    ttbr0_l1[L1_INDEX(0x40000000ULL)] =
        l1_block(0x40000000ULL, ATTRIDX_NORMAL, DESC_SH_INNER);

    /* TTBR1 high-half alias of the SAME physical RAM. With T1SZ=25 the VA
     * 0xFFFFFF80_40000000 maps to PA 0x40000000; L1_INDEX of that VA is 1. */
    ttbr1_l1[L1_INDEX(VMM_TTBR1_BASE + 0x40000000ULL)] =
        l1_block(0x40000000ULL, ATTRIDX_NORMAL, DESC_SH_INNER);
}

/**
 * Program MAIR/TCR/TTBR0/TTBR1, invalidate the TLB, then set SCTLR_EL1.M|C|I.
 * Runs from an identity-mapped low PC, so the instruction after the enabling
 * isb is fetched 1:1 and execution continues with no jump.
 */
static void vmm_enable(void)
{
    /* IPS = ID_AA64MMFR0_EL1.PARange[3:0] (low 3 bits): self-correcting rather
     * than hardcoding the cortex-a57 44-bit (0b100) value. */
    uint64_t mmfr0;
    __asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(mmfr0));
    uint64_t ips = mmfr0 & 0x7ULL;

    /* Attr1 = Normal WB RW-alloc (0xFF), Attr0 = Device-nGnRnE (0x00). */
    uint64_t mair = (0xFFULL << 8) | (0x00ULL << 0);

    /* TCR_EL1 field values for 4KB granule, 39-bit VA both halves (T0SZ=T1SZ=25),
     * Normal-WB Inner-Shareable table walks. NOTE the granule asymmetry: TG0=0b00
     * but TG1=0b10 both select 4KB. Setting TG1=0b00 (the TG0 value) is a
     * notorious bug that faults at level 0 on the first high-half access. */
    uint64_t tcr = (25ULL)              /* T0SZ = 25 -> 2^39 VA */
                 | (1ULL << 8)          /* IRGN0 = Normal Inner WB RW-alloc */
                 | (1ULL << 10)         /* ORGN0 = Normal Outer WB RW-alloc */
                 | (3ULL << 12)         /* SH0   = Inner Shareable */
                 | (0ULL << 14)         /* TG0   = 4KB granule (0b00) */
                 | (25ULL << 16)        /* T1SZ  = 25 -> 2^39 VA */
                 | (1ULL << 24)         /* IRGN1 = Normal Inner WB RW-alloc */
                 | (1ULL << 26)         /* ORGN1 = Normal Outer WB RW-alloc */
                 | (3ULL << 28)         /* SH1   = Inner Shareable */
                 | (2ULL << 30)         /* TG1   = 4KB granule (0b10, NOT 0b00) */
                 | (ips << 32);         /* IPS   = implemented PA size */

    __asm__ volatile("msr mair_el1, %0"  :: "r"(mair));
    __asm__ volatile("msr tcr_el1,  %0"  :: "r"(tcr));
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"((uint64_t)&ttbr0_l1));
    __asm__ volatile("msr ttbr1_el1, %0" :: "r"((uint64_t)&ttbr1_l1));

    /* Publish the table writes, drop any stale TLB entries, then synchronize
     * before consulting the new translation regime. */
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("tlbi vmalle1");
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb");

    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= SCTLR_M | SCTLR_C | SCTLR_I;
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr));
    __asm__ volatile("isb");
}

void vmm_init(void)
{
    vmm_build_tables();
    vmm_enable();
}

uint32_t vmm_ttbr1_alias_read(uint64_t pa)
{
    return *(volatile uint32_t *)(VMM_TTBR1_BASE + pa);
}

void vmm_report(void)
{
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    klog_info("MMU enabled (SCTLR_EL1.M=%u, C=%u, I=%u), TTBR1 kernel map active",
              (unsigned)(sctlr & 1u),
              (unsigned)((sctlr >> 2) & 1u),
              (unsigned)((sctlr >> 12) & 1u));

    /* Read the first word of the kernel image both ways and compare: the
     * identity (TTBR0) view at the physical address, and the TTBR1 high-half
     * alias. A match proves the high-half mapping is live. */
    uint64_t pa = (uint64_t)_kernel_start;
    volatile uint32_t *low = (volatile uint32_t *)pa;
    if (*low == vmm_ttbr1_alias_read(pa)) {
        klog_info("TTBR1 alias verified: high-half read matches physical (%p)",
                  (void *)(VMM_TTBR1_BASE + pa));
    } else {
        klog_error("TTBR1 alias mismatch at %p", (void *)pa);
    }
}

/* ============================================================================
 * End of vmm.c
 * ============================================================================ */
