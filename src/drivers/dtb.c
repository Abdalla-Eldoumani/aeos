/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/drivers/dtb.c
 * Description: Device Tree Blob parser implementation
 * ============================================================================ */

#include <aeos/dtb.h>
#include <aeos/kprintf.h>
#include <aeos/string.h>

/* Global DTB pointer */
static void *g_dtb_addr = NULL;
static dtb_header_t *g_dtb_header = NULL;

/* Endianness conversion helpers */
static inline uint32_t fdt32_to_cpu(uint32_t x)
{
    return ((x & 0xFF000000) >> 24) |
           ((x & 0x00FF0000) >> 8) |
           ((x & 0x0000FF00) << 8) |
           ((x & 0x000000FF) << 24);
}

static inline uint64_t fdt64_to_cpu(uint64_t x)
{
    uint32_t hi = (uint32_t)(x >> 32);
    uint32_t lo = (uint32_t)(x & 0xFFFFFFFF);
    return ((uint64_t)fdt32_to_cpu(lo) << 32) | fdt32_to_cpu(hi);
}

/**
 * Initialize DTB parser
 */
int dtb_init(void *dtb_addr)
{
    if (dtb_addr == NULL) {
        klog_error("DTB address is NULL");
        return -1;
    }

    g_dtb_addr = dtb_addr;
    g_dtb_header = (dtb_header_t *)dtb_addr;

    /* Verify magic number */
    uint32_t magic = fdt32_to_cpu(g_dtb_header->magic);
    if (magic != DTB_MAGIC) {
        klog_error("Invalid DTB magic: 0x%x (expected 0x%x)", magic, DTB_MAGIC);
        return -1;
    }

    klog_info("DTB initialized at 0x%p", dtb_addr);
    klog_debug("  Total size: %u bytes", fdt32_to_cpu(g_dtb_header->totalsize));
    klog_debug("  Version: %u", fdt32_to_cpu(g_dtb_header->version));

    return 0;
}

/**
 * Get pointer to strings block
 */
static const char *dtb_get_string(uint32_t offset)
{
    if (g_dtb_header == NULL) {
        return NULL;
    }

    uint32_t strings_offset = fdt32_to_cpu(g_dtb_header->off_dt_strings);
    return (const char *)((uint8_t *)g_dtb_addr + strings_offset + offset);
}

/**
 * Find framebuffer in device tree
 * Looks for "simple-framebuffer" or "ramfb" compatible devices
 */
int dtb_find_framebuffer(uint64_t *fb_addr, uint64_t *fb_size)
{
    if (g_dtb_header == NULL) {
        klog_error("DTB not initialized");
        return -1;
    }

    uint32_t struct_offset = fdt32_to_cpu(g_dtb_header->off_dt_struct);
    uint32_t *p = (uint32_t *)((uint8_t *)g_dtb_addr + struct_offset);

    bool in_framebuffer_node = false;
    uint64_t reg_addr = 0;
    uint64_t reg_size = 0;

    /* Walk through device tree structure */
    while (1) {
        uint32_t token = fdt32_to_cpu(*p++);

        switch (token) {
        case FDT_BEGIN_NODE: {
            const char *name = (const char *)p;

            /* Check if this is a framebuffer node */
            if (strstr(name, "framebuffer") || strstr(name, "ramfb")) {
                in_framebuffer_node = true;
                klog_debug("Found potential framebuffer node: %s", name);
            }

            /* Skip to next 4-byte boundary */
            p = (uint32_t *)(((uintptr_t)p + strlen(name) + 1 + 3) & ~3);
            break;
        }

        case FDT_END_NODE:
            if (in_framebuffer_node && reg_addr != 0) {
                /* Found framebuffer with valid address */
                *fb_addr = reg_addr;
                *fb_size = reg_size;
                klog_info("Found framebuffer: addr=0x%llx, size=0x%llx", reg_addr, reg_size);
                return 0;
            }
            in_framebuffer_node = false;
            reg_addr = 0;
            reg_size = 0;
            break;

        case FDT_PROP: {
            uint32_t len = fdt32_to_cpu(*p++);
            uint32_t nameoff = fdt32_to_cpu(*p++);
            const char *prop_name = dtb_get_string(nameoff);
            const uint8_t *prop_data = (const uint8_t *)p;

            if (in_framebuffer_node) {
                /* Look for "reg" property (address and size) */
                if (strcmp(prop_name, "reg") == 0 && len >= 16) {
                    /* Assuming #address-cells=2, #size-cells=2 */
                    const uint64_t *reg = (const uint64_t *)prop_data;
                    reg_addr = fdt64_to_cpu(reg[0]);
                    reg_size = fdt64_to_cpu(reg[1]);
                    klog_debug("  reg property: addr=0x%llx, size=0x%llx", reg_addr, reg_size);
                }

                /* Check compatible string */
                if (strcmp(prop_name, "compatible") == 0) {
                    klog_debug("  compatible: %s", (const char *)prop_data);
                }
            }

            /* Skip to next 4-byte boundary */
            p = (uint32_t *)(((uintptr_t)prop_data + len + 3) & ~3);
            break;
        }

        case FDT_NOP:
            break;

        case FDT_END:
            /* Reached end of device tree */
            klog_warn("Framebuffer not found in device tree");
            return -1;

        default:
            klog_error("Unknown DTB token: 0x%x", token);
            return -1;
        }
    }

    return -1;
}

/* ============================================================================
 * End of dtb.c
 * ============================================================================ */
