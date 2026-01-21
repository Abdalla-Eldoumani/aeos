/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/desktop.h
 * Description: Desktop environment interface
 * ============================================================================ */

#ifndef AEOS_DESKTOP_H
#define AEOS_DESKTOP_H

#include <aeos/types.h>
#include <aeos/window.h>

/* Desktop icon dimensions */
#define ICON_WIDTH      48
#define ICON_HEIGHT     48
#define ICON_LABEL_HEIGHT 16
#define ICON_SPACING    80
#define ICON_MARGIN     20

/* Taskbar dimensions */
#define TASKBAR_HEIGHT  32
#define TASKBAR_BUTTON_WIDTH 120
#define START_BUTTON_WIDTH 60

/* Maximum desktop icons */
#define MAX_DESKTOP_ICONS 16

/* Desktop icon structure */
typedef struct {
    char name[32];
    int32_t x;
    int32_t y;
    bool selected;
    void (*on_launch)(void);
    uint32_t icon_color;  /* Primary color for simple icon */
} desktop_icon_t;

/**
 * Initialize the desktop environment
 */
void desktop_init(void);

/**
 * Draw the desktop background
 */
void desktop_draw_background(void);

/**
 * Draw desktop icons
 */
void desktop_draw_icons(void);

/**
 * Draw the taskbar
 */
void desktop_draw_taskbar(void);

/**
 * Full desktop paint (background + icons + taskbar)
 * This is the callback for window manager
 */
void desktop_paint(void);

/**
 * Handle desktop click (for icons)
 * @param x Screen X coordinate
 * @param y Screen Y coordinate
 * @param double_click True if double-click
 */
void desktop_handle_click(int32_t x, int32_t y, bool double_click);

/**
 * Add a desktop icon
 * @param name Icon label
 * @param icon_color Color for simple icon
 * @param on_launch Callback when icon is launched
 * @return Index of icon or -1 on error
 */
int desktop_add_icon(const char *name, uint32_t icon_color, void (*on_launch)(void));

/**
 * Update taskbar (call when window list changes)
 */
void desktop_update_taskbar(void);

/**
 * Check if click is on taskbar
 */
bool desktop_is_taskbar_click(int32_t y);

/**
 * Handle taskbar click
 */
void desktop_handle_taskbar_click(int32_t x, int32_t y);

/**
 * Get taskbar height (for window positioning)
 */
uint32_t desktop_get_taskbar_height(void);

/**
 * Show/hide start menu
 */
void desktop_toggle_start_menu(void);

/**
 * Is start menu visible?
 */
bool desktop_start_menu_visible(void);

#endif /* AEOS_DESKTOP_H */
