/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/ramfb.h
 * Description: QEMU ramfb (RAM framebuffer) driver
 * ============================================================================ */

#ifndef AEOS_RAMFB_H
#define AEOS_RAMFB_H

#include <aeos/types.h>

/* ramfb configuration structure (written to fw_cfg) */
typedef struct {
    uint64_t addr;      /* Framebuffer physical address */
    uint32_t fourcc;    /* Pixel format (DRM fourcc code) */
    uint32_t flags;     /* Flags */
    uint32_t width;     /* Width in pixels */
    uint32_t height;    /* Height in pixels */
    uint32_t stride;    /* Bytes per line */
} __attribute__((packed)) ramfb_cfg_t;

/* DRM fourcc pixel formats */
#define DRM_FORMAT_XRGB8888  0x34325258  /* 32-bit RGB (8:8:8) */
#define DRM_FORMAT_ARGB8888  0x34325241  /* 32-bit ARGB (8:8:8:8) */

/**
 * Initialize ramfb driver
 * Uses the existing framebuffer from fb_init()
 * @return 0 on success, -1 on error
 */
int ramfb_init(void);

/**
 * Update ramfb display
 * Copies current framebuffer to ramfb device memory
 * @return 0 on success, -1 on error
 */
int ramfb_update(void);

/**
 * Enable ramfb output
 * Configures QEMU to display our framebuffer
 * @return 0 on success, -1 on error
 */
int ramfb_enable(void);

#endif /* AEOS_RAMFB_H */

/* ============================================================================
 * End of ramfb.h
 * ============================================================================ */
