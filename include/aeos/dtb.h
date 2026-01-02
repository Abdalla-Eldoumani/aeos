/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/dtb.h
 * Description: Device Tree Blob (DTB) parser
 * ============================================================================ */

#ifndef AEOS_DTB_H
#define AEOS_DTB_H

#include <aeos/types.h>

/* DTB magic number */
#define DTB_MAGIC 0xd00dfeed

/* DTB tokens */
#define FDT_BEGIN_NODE  0x00000001
#define FDT_END_NODE    0x00000002
#define FDT_PROP        0x00000003
#define FDT_NOP         0x00000004
#define FDT_END         0x00000009

/* Device Tree header */
typedef struct {
    uint32_t magic;              /* 0xd00dfeed */
    uint32_t totalsize;          /* Total size of DTB */
    uint32_t off_dt_struct;      /* Offset to structure block */
    uint32_t off_dt_strings;     /* Offset to strings block */
    uint32_t off_mem_rsvmap;     /* Offset to memory reserve map */
    uint32_t version;            /* Version */
    uint32_t last_comp_version;  /* Last compatible version */
    uint32_t boot_cpuid_phys;    /* Physical CPU ID */
    uint32_t size_dt_strings;    /* Size of strings block */
    uint32_t size_dt_struct;     /* Size of structure block */
} __attribute__((packed)) dtb_header_t;

/**
 * Initialize DTB parser
 * @param dtb_addr Address of device tree blob
 * @return 0 on success, -1 on error
 */
int dtb_init(void *dtb_addr);

/**
 * Find framebuffer address in device tree
 * @param fb_addr Output: framebuffer base address
 * @param fb_size Output: framebuffer size
 * @return 0 on success, -1 if not found
 */
int dtb_find_framebuffer(uint64_t *fb_addr, uint64_t *fb_size);

#endif /* AEOS_DTB_H */

/* ============================================================================
 * End of dtb.h
 * ============================================================================ */
