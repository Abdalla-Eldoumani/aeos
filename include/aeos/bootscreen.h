/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/bootscreen.h
 * Description: Animated boot screen interface
 * ============================================================================ */

#ifndef AEOS_BOOTSCREEN_H
#define AEOS_BOOTSCREEN_H

#include <aeos/types.h>

/* Boot stages */
typedef enum {
    BOOT_STAGE_MEMORY = 0,
    BOOT_STAGE_INTERRUPTS,
    BOOT_STAGE_TIMER,
    BOOT_STAGE_FILESYSTEM,
    BOOT_STAGE_PROCESSES,
    BOOT_STAGE_INPUT,
    BOOT_STAGE_DESKTOP,
    BOOT_STAGE_COMPLETE,
    BOOT_STAGE_COUNT = BOOT_STAGE_COMPLETE
} boot_stage_t;

/* Boot stage info */
typedef struct {
    const char *message;
    uint32_t progress;  /* 0-100 */
} boot_stage_info_t;

/**
 * Initialize and display boot screen
 * Must be called after fb_init() and virtio_gpu_init()
 */
void bootscreen_init(void);

/**
 * Update boot progress to specified stage
 * @param stage Current boot stage
 */
void bootscreen_update(boot_stage_t stage);

/**
 * Set custom boot message
 * @param message Message to display
 * @param progress Progress percentage (0-100)
 */
void bootscreen_set_progress(const char *message, uint32_t progress);

/**
 * Complete boot screen and transition to desktop
 * Returns true if user wants GUI mode, false for text mode
 */
bool bootscreen_complete(void);

/**
 * Check if 'T' key was pressed during boot for text mode
 * @return true if text mode requested
 */
bool bootscreen_text_mode_requested(void);

/**
 * Draw the AEOS logo
 */
void bootscreen_draw_logo(uint32_t x, uint32_t y);

#endif /* AEOS_BOOTSCREEN_H */
