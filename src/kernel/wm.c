/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/kernel/wm.c
 * Description: Window Manager implementation
 * ============================================================================ */

#include <aeos/wm.h>
#include <aeos/window.h>
#include <aeos/event.h>
#include <aeos/framebuffer.h>
#include <aeos/virtio_gpu.h>
#include <aeos/virtio_input.h>
#include <aeos/desktop.h>
#include <aeos/timer.h>
#include <aeos/kprintf.h>
#include <aeos/string.h>

/* Window manager state */
static struct {
    window_t *window_list;      /* Head of window list (bottom) */
    window_t *top_window;       /* Top window (focused) */
    window_t *focused;          /* Currently focused window */
    uint32_t window_count;
    bool initialized;
    bool should_exit;
    bool needs_redraw;

    /* Mouse state */
    int32_t mouse_x;
    int32_t mouse_y;
    bool mouse_visible;

    /* Dragging state */
    window_t *drag_window;
    int32_t drag_start_x;
    int32_t drag_start_y;

    /* Desktop paint callback */
    wm_desktop_paint_fn desktop_paint;

    /* Cursor backup buffer */
    uint32_t cursor_backup[CURSOR_WIDTH * CURSOR_HEIGHT];
    int32_t cursor_backup_x;
    int32_t cursor_backup_y;
    bool cursor_backup_valid;
} wm;

/* Mouse cursor bitmap (arrow) */
static const uint8_t cursor_bitmap[CURSOR_HEIGHT][CURSOR_WIDTH] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,2,2,2,1,0},
    {1,2,2,2,2,2,2,1,1,1,1,1},
    {1,2,2,2,1,2,2,1,0,0,0,0},
    {1,2,2,1,0,1,2,2,1,0,0,0},
    {1,2,1,0,0,1,2,2,1,0,0,0},
    {1,1,0,0,0,0,1,2,2,1,0,0},
    {1,0,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,0,1,2,2,1,0},
    {0,0,0,0,0,0,0,1,2,2,1,0},
    {0,0,0,0,0,0,0,0,1,1,0,0}
};

/**
 * Save area under cursor
 */
static void save_cursor_background(int32_t x, int32_t y)
{
    int32_t i, j;
    fb_info_t *fb = fb_get_info();

    if (!fb || !fb->initialized) {
        return;
    }

    wm.cursor_backup_x = x;
    wm.cursor_backup_y = y;
    wm.cursor_backup_valid = true;

    for (j = 0; j < CURSOR_HEIGHT; j++) {
        for (i = 0; i < CURSOR_WIDTH; i++) {
            int32_t px = x + i;
            int32_t py = y + j;
            if (px >= 0 && px < (int32_t)fb->width &&
                py >= 0 && py < (int32_t)fb->height) {
                wm.cursor_backup[j * CURSOR_WIDTH + i] = fb_getpixel(px, py);
            }
        }
    }
}

/**
 * Restore area under cursor
 */
static void restore_cursor_background(void)
{
    int32_t i, j;
    fb_info_t *fb = fb_get_info();

    if (!fb || !fb->initialized || !wm.cursor_backup_valid) {
        return;
    }

    for (j = 0; j < CURSOR_HEIGHT; j++) {
        for (i = 0; i < CURSOR_WIDTH; i++) {
            int32_t px = wm.cursor_backup_x + i;
            int32_t py = wm.cursor_backup_y + j;
            if (px >= 0 && px < (int32_t)fb->width &&
                py >= 0 && py < (int32_t)fb->height) {
                fb_putpixel(px, py, wm.cursor_backup[j * CURSOR_WIDTH + i]);
            }
        }
    }

    wm.cursor_backup_valid = false;
}

/**
 * Initialize window manager
 */
void wm_init(void)
{
    klog_info("Initializing window manager...");

    memset(&wm, 0, sizeof(wm));

    wm.window_list = NULL;
    wm.top_window = NULL;
    wm.focused = NULL;
    wm.window_count = 0;
    wm.initialized = true;
    wm.should_exit = false;
    wm.needs_redraw = true;

    wm.mouse_x = FB_WIDTH / 2;
    wm.mouse_y = FB_HEIGHT / 2;
    wm.mouse_visible = true;

    wm.drag_window = NULL;
    wm.desktop_paint = NULL;
    wm.cursor_backup_valid = false;

    klog_info("Window manager initialized");
}

/**
 * Register a window
 */
void wm_register_window(window_t *win)
{
    if (!win || !wm.initialized) {
        return;
    }

    /* Add to end of list (on top) */
    if (wm.window_list == NULL) {
        wm.window_list = win;
        wm.top_window = win;
        win->prev = NULL;
        win->next = NULL;
    } else {
        /* Find end of list */
        window_t *tail = wm.top_window;
        tail->next = win;
        win->prev = tail;
        win->next = NULL;
        wm.top_window = win;
    }

    win->z_order = wm.window_count;
    wm.window_count++;

    /* Focus new window */
    wm_focus_window(win);

    wm.needs_redraw = true;

    klog_debug("Registered window %u: '%s' (total: %u)",
               win->id, win->title, wm.window_count);
}

/**
 * Unregister a window
 */
void wm_unregister_window(window_t *win)
{
    if (!win || !wm.initialized) {
        return;
    }

    /* Clear drag state if we're removing the window being dragged */
    if (wm.drag_window == win) {
        wm.drag_window = NULL;
    }

    /* Remove from list */
    if (win->prev) {
        win->prev->next = win->next;
    } else {
        wm.window_list = win->next;
    }

    if (win->next) {
        win->next->prev = win->prev;
    } else {
        wm.top_window = win->prev;
    }

    wm.window_count--;

    /* Update focus if needed */
    if (wm.focused == win) {
        wm.focused = wm.top_window;
        if (wm.focused) {
            wm.focused->flags |= WINDOW_FLAG_FOCUSED;
        }
    }

    wm.needs_redraw = true;

    klog_debug("Unregistered window %u: '%s' (remaining: %u)",
               win->id, win->title, wm.window_count);
}

/**
 * Bring window to front
 */
void wm_focus_window(window_t *win)
{
    if (!win || !wm.initialized) {
        return;
    }

    /* Already focused and on top? */
    if (win == wm.top_window && win == wm.focused) {
        return;
    }

    /* Remove focus from old window */
    if (wm.focused && wm.focused != win) {
        wm.focused->flags &= ~WINDOW_FLAG_FOCUSED;
        wm.focused->flags |= WINDOW_FLAG_DIRTY;
    }

    /* Move window to top of list */
    if (win != wm.top_window) {
        /* Remove from current position */
        if (win->prev) {
            win->prev->next = win->next;
        } else {
            wm.window_list = win->next;
        }
        if (win->next) {
            win->next->prev = win->prev;
        }

        /* Add to end (top) */
        win->prev = wm.top_window;
        win->next = NULL;
        if (wm.top_window) {
            wm.top_window->next = win;
        }
        wm.top_window = win;
    }

    /* Set focus */
    win->flags |= WINDOW_FLAG_FOCUSED | WINDOW_FLAG_DIRTY;
    wm.focused = win;

    wm.needs_redraw = true;
}

/**
 * Get focused window
 */
window_t *wm_get_focused_window(void)
{
    return wm.focused;
}

/**
 * Find window at position
 */
window_t *wm_window_at(int32_t x, int32_t y)
{
    window_t *win;

    /* Search from top to bottom */
    for (win = wm.top_window; win != NULL; win = win->prev) {
        if (window_contains_point(win, x, y)) {
            return win;
        }
    }

    return NULL;
}

/**
 * Redraw all windows
 */
void wm_redraw(void)
{
    window_t *win;

    /* Draw desktop background */
    if (wm.desktop_paint) {
        wm.desktop_paint();
    } else {
        /* Default background */
        fb_clear(0xFF202040);
    }

    /* Draw windows from bottom to top */
    for (win = wm.window_list; win != NULL; win = win->next) {
        if (win->flags & WINDOW_FLAG_VISIBLE) {
            window_draw(win);
        }
    }

    wm.needs_redraw = false;
}

/**
 * Draw mouse cursor
 */
void wm_draw_cursor(void)
{
    int32_t i, j;
    uint32_t color;
    fb_info_t *fb = fb_get_info();

    if (!fb || !fb->initialized || !wm.mouse_visible) {
        return;
    }

    /* Save background */
    save_cursor_background(wm.mouse_x, wm.mouse_y);

    /* Draw cursor */
    for (j = 0; j < CURSOR_HEIGHT; j++) {
        for (i = 0; i < CURSOR_WIDTH; i++) {
            int32_t px = wm.mouse_x + i;
            int32_t py = wm.mouse_y + j;

            if (px < 0 || px >= (int32_t)fb->width ||
                py < 0 || py >= (int32_t)fb->height) {
                continue;
            }

            switch (cursor_bitmap[j][i]) {
                case 1:  /* Black outline */
                    color = 0xFF000000;
                    break;
                case 2:  /* White fill */
                    color = 0xFFFFFFFF;
                    break;
                default:  /* Transparent */
                    continue;
            }

            fb_putpixel(px, py, color);
        }
    }
}

/**
 * Update display
 */
void wm_update_display(void)
{
    /* Restore cursor background before redraw */
    restore_cursor_background();

    /* Redraw if needed */
    if (wm.needs_redraw) {
        wm_redraw();
        wm.needs_redraw = false;  /* Clear flag after redraw */
    }

    /* Draw cursor */
    wm_draw_cursor();

    /* Send to GPU (silently) */
    virtio_gpu_update_display();
}

/**
 * Handle mouse button event
 */
static void handle_mouse_button(mouse_event_t *mouse, bool pressed)
{
    window_t *win;

    if (pressed && (mouse->buttons & MOUSE_BUTTON_LEFT)) {
        /* Left click */
        win = wm_window_at(mouse->x, mouse->y);

        if (win) {
            /* Focus window */
            wm_focus_window(win);

            /* Check close button */
            if (window_in_close_button(win, mouse->x, mouse->y)) {
                if (win->on_close) {
                    win->on_close(win);
                } else {
                    /* Default: destroy window */
                    wm_unregister_window(win);
                    window_destroy(win);
                }
                return;
            }

            /* Check title bar for dragging */
            if (window_in_title_bar(win, mouse->x, mouse->y)) {
                wm.drag_window = win;
                wm.drag_start_x = mouse->x - win->x;
                wm.drag_start_y = mouse->y - win->y;
                win->flags |= WINDOW_FLAG_DRAGGING;
                return;
            }

            /* Pass to window callback */
            if (win->on_mouse) {
                /* Convert to window-relative coordinates (manual copy for AArch64 safety) */
                mouse_event_t local;
                local.x = mouse->x - win->client_x;
                local.y = mouse->y - win->client_y;
                local.buttons = mouse->buttons;
                local.scroll = mouse->scroll;
                win->on_mouse(win, &local);
            }
        } else {
            /* Click on desktop or taskbar (not on any window) */
            if (desktop_is_taskbar_click(mouse->y)) {
                desktop_handle_taskbar_click(mouse->x, mouse->y);
            } else {
                desktop_handle_click(mouse->x, mouse->y, false);
            }
            wm.needs_redraw = true;
        }
    } else if (!pressed) {
        /* Button released */
        if (wm.drag_window) {
            wm.drag_window->flags &= ~WINDOW_FLAG_DRAGGING;
            wm.drag_window = NULL;
        }
    }
}

/**
 * Handle mouse move event
 */
static void handle_mouse_move(mouse_event_t *mouse)
{
    /* Update cursor position */
    wm.mouse_x = mouse->x;
    wm.mouse_y = mouse->y;

    /* Cursor is drawn via save/restore, so no full redraw needed for moves.
       Only set needs_redraw when dragging (window position changes). */

    /* Handle dragging */
    if (wm.drag_window) {
        wm.needs_redraw = true;
        int32_t new_x = mouse->x - wm.drag_start_x;
        int32_t new_y = mouse->y - wm.drag_start_y;

        /* Clamp to screen bounds */
        if (new_x < -(int32_t)(wm.drag_window->width - 40)) {
            new_x = -(int32_t)(wm.drag_window->width - 40);
        }
        if (new_x > (int32_t)FB_WIDTH - 40) {
            new_x = FB_WIDTH - 40;
        }
        if (new_y < 0) new_y = 0;
        if (new_y > (int32_t)FB_HEIGHT - WINDOW_TITLE_HEIGHT) {
            new_y = FB_HEIGHT - WINDOW_TITLE_HEIGHT;
        }

        window_move(wm.drag_window, new_x, new_y);
    }
}

/**
 * Handle key event
 */
static void handle_key(key_event_t *key, bool pressed)
{
    if (!pressed) {
        return;  /* Only handle key down */
    }

    /* Debug: show key in title bar to verify keyboard events reach WM */
    if (wm.focused) {
        char debug_title[WINDOW_TITLE_MAX];
        /* Find base title (strip previous debug suffix) */
        char base[WINDOW_TITLE_MAX];
        strncpy(base, wm.focused->title, WINDOW_TITLE_MAX - 1);
        base[WINDOW_TITLE_MAX - 1] = '\0';
        char *bracket = strchr(base, '[');
        if (bracket && bracket > base) {
            /* Trim trailing space before bracket */
            bracket--;
            while (bracket > base && *bracket == ' ') bracket--;
            bracket[1] = '\0';
        }
        if (key->ascii >= 32 && key->ascii < 127) {
            snprintf(debug_title, sizeof(debug_title), "%s [%c]", base, key->ascii);
        } else {
            snprintf(debug_title, sizeof(debug_title), "%s [k%d]", base, key->keycode);
        }
        window_set_title(wm.focused, debug_title);
        wm.needs_redraw = true;
    }

    /* Pass to focused window */
    if (wm.focused && wm.focused->on_key) {
        wm.focused->on_key(wm.focused, key);
    }
}

/**
 * Handle an event
 */
void wm_handle_event(event_t *event)
{
    switch (event->type) {
        case EVENT_MOUSE_MOVE:
            handle_mouse_move(&event->data.mouse);
            break;

        case EVENT_MOUSE_BUTTON_DOWN:
            handle_mouse_button(&event->data.mouse, true);
            break;

        case EVENT_MOUSE_BUTTON_UP:
            handle_mouse_button(&event->data.mouse, false);
            break;

        case EVENT_KEY_DOWN:
            handle_key(&event->data.key, true);
            break;

        case EVENT_KEY_UP:
            handle_key(&event->data.key, false);
            break;

        default:
            break;
    }
}

/**
 * Main window manager loop
 */
void wm_run(void)
{
    event_t event;
    uint64_t last_update = 0;
    uint64_t now;

    klog_info("Starting window manager main loop");

    while (!wm.should_exit) {
        /* Poll input devices */
        event_poll();
        virtio_input_poll();

        /* Process all pending events */
        while (event_pop(&event)) {
            wm_handle_event(&event);
        }

        /* Update display periodically (30 FPS) */
        now = timer_get_uptime_ms();
        if (now - last_update >= 33 || wm.needs_redraw) {
            wm_update_display();
            last_update = now;
        }

        /* Small delay to prevent busy looping */
        timer_delay_ms(1);
    }

    klog_info("Window manager exiting");
}

/**
 * Request exit
 */
void wm_request_exit(void)
{
    wm.should_exit = true;
}

/**
 * Check if should exit
 */
bool wm_should_exit(void)
{
    return wm.should_exit;
}

/**
 * Set desktop paint callback
 */
void wm_set_desktop_paint(wm_desktop_paint_fn fn)
{
    wm.desktop_paint = fn;
}

/**
 * Get window list
 */
window_t *wm_get_window_list(void)
{
    return wm.window_list;
}

/**
 * Get window count
 */
uint32_t wm_get_window_count(void)
{
    return wm.window_count;
}

/* ============================================================================
 * End of wm.c
 * ============================================================================ */
