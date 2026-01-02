/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/drivers/ramfb.c
 * Description: QEMU ramfb (RAM framebuffer) driver implementation
 * ============================================================================ */

#include <aeos/ramfb.h>
#include <aeos/framebuffer.h>
#include <aeos/dtb.h>
#include <aeos/kprintf.h>
#include <aeos/string.h>

/* fw_cfg MMIO addresses for QEMU virt */
#define FW_CFG_SELECTOR  0x09020008
#define FW_CFG_DATA      0x09020000

/* fw_cfg file selectors */
#define FW_CFG_FILE_DIR  0x0019

/* ramfb state */
static ramfb_cfg_t ramfb_config;
static bool ramfb_initialized = false;

/* Helper functions for fw_cfg access */
static inline void fw_cfg_select(uint16_t key)
{
    volatile uint16_t *selector = (volatile uint16_t *)FW_CFG_SELECTOR;
    *selector = key;
}

static inline uint8_t fw_cfg_read_byte(void)
{
    volatile uint8_t *data = (volatile uint8_t *)FW_CFG_DATA;
    return *data;
}

static inline void fw_cfg_read(void *buf, uint32_t len)
{
    uint8_t *ptr = (uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++) {
        ptr[i] = fw_cfg_read_byte();
    }
}

static inline void fw_cfg_write(const void *buf, uint32_t len)
{
    volatile uint8_t *data = (volatile uint8_t *)FW_CFG_DATA;
    const uint8_t *ptr = (const uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++) {
        data[i] = ptr[i];
    }
}

/* Convert big-endian uint32 to host byte order */
static inline uint32_t be32_to_cpu(uint32_t be_val)
{
    return ((be_val & 0xFF000000) >> 24) |
           ((be_val & 0x00FF0000) >>  8) |
           ((be_val & 0x0000FF00) <<  8) |
           ((be_val & 0x000000FF) << 24);
}

/* Convert host byte order to big-endian */
static inline uint64_t cpu_to_be64(uint64_t val)
{
    return ((val & 0xFF00000000000000ULL) >> 56) |
           ((val & 0x00FF000000000000ULL) >> 40) |
           ((val & 0x0000FF0000000000ULL) >> 24) |
           ((val & 0x000000FF00000000ULL) >>  8) |
           ((val & 0x00000000FF000000ULL) <<  8) |
           ((val & 0x0000000000FF0000ULL) << 24) |
           ((val & 0x000000000000FF00ULL) << 40) |
           ((val & 0x00000000000000FFULL) << 56);
}

static inline uint32_t cpu_to_be32(uint32_t val)
{
    return ((val & 0xFF000000) >> 24) |
           ((val & 0x00FF0000) >>  8) |
           ((val & 0x0000FF00) <<  8) |
           ((val & 0x000000FF) << 24);
}

/**
 * Check if fw_cfg is accessible
 * Returns true if we can safely access fw_cfg
 */
// static bool fw_cfg_is_available(void)
// {
//     /* For now, we'll just assume fw_cfg is available if QEMU was started
//      * with -device ramfb. If accessing fw_cfg causes an exception,
//      * the exception handler will catch it.
//      *
//      * A more robust implementation would check the device tree or
//      * try a test read with exception handling.
//      */
//     return true;  /* Optimistically assume it's available */
// }

/**
 * Initialize ramfb driver
 */
int ramfb_init(void)
{
    fb_info_t *fb;

    klog_debug("Checking for ramfb device...");

    /* Get framebuffer info - safely */
    fb = fb_get_info();
    if (fb == NULL) {
        klog_debug("Framebuffer info not available");
        return -1;
    }

    if (!fb->initialized) {
        klog_debug("Framebuffer not initialized yet");
        return -1;
    }

    /* Configure ramfb structure */
    ramfb_config.addr = (uint64_t)(uintptr_t)fb->base;
    ramfb_config.fourcc = DRM_FORMAT_XRGB8888;
    ramfb_config.flags = 0;
    ramfb_config.width = fb->width;
    ramfb_config.height = fb->height;
    ramfb_config.stride = fb->pitch;

    ramfb_initialized = true;

    klog_info("ramfb driver ready (virtual framebuffer mode)");
    klog_info("  Resolution: %ux%u @ 0x%p", fb->width, fb->height, fb->base);

    return 0;
}

/**
 * Find a file in fw_cfg file directory by name
 * Returns the selector value, or 0 if not found
 */
static uint16_t fw_cfg_find_file(const char *name)
{
    typedef struct {
        uint32_t size;
        uint16_t selector;
        uint16_t reserved;
        char name[56];
    } __attribute__((packed)) fw_cfg_file_t;

    uint32_t count;
    fw_cfg_file_t file;

    /* Read file directory */
    fw_cfg_select(FW_CFG_FILE_DIR);

    /* First 4 bytes = number of files (big-endian) */
    fw_cfg_read(&count, sizeof(count));
    count = be32_to_cpu(count);

    klog_debug("fw_cfg: scanning %u files for '%s'", count, name);

    /* Scan through files */
    for (uint32_t i = 0; i < count; i++) {
        fw_cfg_read(&file, sizeof(file));

        if (strcmp(file.name, name) == 0) {
            uint16_t selector = be32_to_cpu(file.selector) >> 16;
            klog_debug("fw_cfg: found '%s' at selector 0x%x", name, selector);
            return selector;
        }
    }

    return 0;  /* Not found */
}

/**
 * Enable ramfb by writing configuration
 * Note: This requires QEMU to be started with -device ramfb
 */
int ramfb_enable(void)
{
    uint16_t selector;
    ramfb_cfg_t cfg_be;  /* Big-endian version */

    if (!ramfb_initialized) {
        klog_error("ramfb not initialized");
        return -1;
    }

    klog_debug("Configuring ramfb via fw_cfg...");

    /* Find the etc/ramfb file */
    selector = fw_cfg_find_file("etc/ramfb");
    if (selector == 0) {
        klog_error("fw_cfg file 'etc/ramfb' not found");
        klog_error("Make sure QEMU is started with: -device ramfb");
        return -1;
    }

    /* Convert config to big-endian */
    cfg_be.addr = cpu_to_be64(ramfb_config.addr);
    cfg_be.fourcc = cpu_to_be32(ramfb_config.fourcc);
    cfg_be.flags = cpu_to_be32(ramfb_config.flags);
    cfg_be.width = cpu_to_be32(ramfb_config.width);
    cfg_be.height = cpu_to_be32(ramfb_config.height);
    cfg_be.stride = cpu_to_be32(ramfb_config.stride);

    /* Write configuration */
    fw_cfg_select(selector);
    fw_cfg_write(&cfg_be, sizeof(cfg_be));

    klog_info("[OK] ramfb configured successfully!");
    klog_info("  Address: 0x%llx", ramfb_config.addr);
    klog_info("  Resolution: %ux%u", ramfb_config.width, ramfb_config.height);

    return 0;
}

/**
 * Update ramfb display
 * Since ramfb reads directly from our framebuffer memory,
 * no explicit update is needed - changes are immediate
 */
int ramfb_update(void)
{
    if (!ramfb_initialized) {
        return -1;
    }

    /* ramfb reads directly from our framebuffer memory,
     * so updates happen automatically */
    return 0;
}

/* ============================================================================
 * End of ramfb.c
 * ============================================================================ */
