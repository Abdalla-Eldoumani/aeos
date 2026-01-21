/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/wm.h
 * Description: Window Manager interface
 * ============================================================================ */

#ifndef AEOS_WM_H
#define AEOS_WM_H

#include <aeos/types.h>
#include <aeos/window.h>
#include <aeos/event.h>

/* Mouse cursor size */
#define CURSOR_WIDTH    12
#define CURSOR_HEIGHT   20

/**
 * Initialize the window manager
 */
void wm_init(void);

/**
 * Register a window with the window manager
 */
void wm_register_window(window_t *win);

/**
 * Unregister a window from the window manager
 */
void wm_unregister_window(window_t *win);

/**
 * Bring window to front (focus)
 */
void wm_focus_window(window_t *win);

/**
 * Get the currently focused window
 */
window_t *wm_get_focused_window(void);

/**
 * Find window at screen position
 */
window_t *wm_window_at(int32_t x, int32_t y);

/**
 * Redraw all windows (composite)
 */
void wm_redraw(void);

/**
 * Redraw screen and update display
 */
void wm_update_display(void);

/**
 * Draw mouse cursor at current position
 */
void wm_draw_cursor(void);

/**
 * Handle an event
 */
void wm_handle_event(event_t *event);

/**
 * Main window manager loop
 * Processes events and redraws
 */
void wm_run(void);

/**
 * Request window manager to exit
 */
void wm_request_exit(void);

/**
 * Check if window manager should exit
 */
bool wm_should_exit(void);

/**
 * Set desktop paint callback
 * Called to draw the desktop background before windows
 */
typedef void (*wm_desktop_paint_fn)(void);
void wm_set_desktop_paint(wm_desktop_paint_fn fn);

/**
 * Get list of windows for taskbar
 */
window_t *wm_get_window_list(void);

/**
 * Get number of visible windows
 */
uint32_t wm_get_window_count(void);

#endif /* AEOS_WM_H */
