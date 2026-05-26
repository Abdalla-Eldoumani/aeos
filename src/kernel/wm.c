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
#include <aeos/notify.h>
#include <aeos/timer.h>
#include <aeos/kprintf.h>
#include <aeos/string.h>
#include <aeos/theme.h>

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

    /* Alt+Tab cycle state. While Alt is held, each Tab press advances the
     * focused window WITHOUT changing z-order, so the user can cycle past
     * more than two windows. The focused window is raised on Alt release. */
    bool alt_tab_active;

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

    /* Stamp open animation start so window_draw fades + slides it in */
    win->open_anim_start_ms = timer_get_uptime_ms();
    win->close_anim_start_ms = 0;

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

    /* BUG-20 trace: confirms a full content redraw (on_paint for every visible
     * window) actually ran. If the click trace fires but this does not, the
     * redraw branch was skipped. If both fire but the screen is stale, the
     * problem is past the framebuffer (GPU transfer/flush or host display). */
    klog_debug("wm_redraw: %u windows, focused=%u", wm.window_count,
               wm.focused ? wm.focused->id : 0);

    /* Draw desktop background */
    if (wm.desktop_paint) {
        wm.desktop_paint();
    } else {
        /* Default background when no desktop paint hook is set */
        fb_clear(THEME_BG_DEEP);
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

    /* Redraw if needed. Active toasts force a redraw every frame so the
     * region under a sliding/fading toast is fresh — otherwise stale window
     * content would show through where the toast used to be. */
    if (wm.needs_redraw || notify_active()) {
        wm_redraw();
        wm.needs_redraw = false;
    }

    /* Composite toasts above windows but below the cursor. */
    notify_render();

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

        /* BUG-20 trace: where did the click land and which window caught it.
         * If the reported (x,y) is not where the user aimed, the relative
         * virtio-mouse cursor has desynced from the host pointer. */
        klog_debug("click @ (%d,%d) -> win=%u", mouse->x, mouse->y,
                   win ? win->id : 0);

        if (win) {
            /* Ignore events on a window that's already running its close animation */
            if (win->flags & WINDOW_FLAG_CLOSING) {
                return;
            }

            /* Focus window */
            wm_focus_window(win);

            /* Check close button */
            if (window_in_close_button(win, mouse->x, mouse->y)) {
                /* Defer the actual destruction. wm_reap_closing_windows runs
                 * the on_close handler once the fade-out completes. */
                win->flags |= WINDOW_FLAG_CLOSING;
                win->close_anim_start_ms = timer_get_uptime_ms();
                wm.needs_redraw = true;
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
                klog_debug("  dispatch on_mouse win=%u client-local=(%d,%d)",
                           win->id, local.x, local.y);
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

        /* Keep at least 32 px of the title bar on-screen so the user can
         * always grab the window again. */
        const int32_t MIN_VISIBLE = 32;
        int32_t w = (int32_t)wm.drag_window->width;
        if (new_x < MIN_VISIBLE - w) {
            new_x = MIN_VISIBLE - w;
        }
        if (new_x > (int32_t)FB_WIDTH - MIN_VISIBLE) {
            new_x = (int32_t)FB_WIDTH - MIN_VISIBLE;
        }
        if (new_y < 0) new_y = 0;
        if (new_y > (int32_t)FB_HEIGHT - WINDOW_TITLE_HEIGHT) {
            new_y = (int32_t)FB_HEIGHT - WINDOW_TITLE_HEIGHT;
        }

        window_move(wm.drag_window, new_x, new_y);
    }
}

/**
 * Set focus on `win` without changing the window z-order. Used during
 * Alt+Tab cycling so the user can step past more than two windows.
 */
static void focus_no_raise(window_t *win)
{
    if (!win || win == wm.focused) {
        return;
    }
    if (wm.focused) {
        wm.focused->flags &= ~WINDOW_FLAG_FOCUSED;
        wm.focused->flags |= WINDOW_FLAG_DIRTY;
    }
    wm.focused = win;
    win->flags |= WINDOW_FLAG_FOCUSED | WINDOW_FLAG_DIRTY;
    wm.needs_redraw = true;
}

/**
 * Walk z-order downward (newest to oldest) from `current`, skipping
 * invisible or closing windows, wrapping back to the top once we hit the
 * bottom. Returns `current` if no other candidate exists.
 */
static window_t *next_focus_candidate(window_t *current)
{
    window_t *c;

    if (!current) {
        return wm.top_window;
    }
    for (c = current->prev; c != NULL; c = c->prev) {
        if ((c->flags & WINDOW_FLAG_VISIBLE) &&
            !(c->flags & WINDOW_FLAG_CLOSING)) {
            return c;
        }
    }
    /* Wrap: walk down from top until we either find a candidate other than
     * `current`, or end up back at `current` itself. */
    for (c = wm.top_window; c != NULL && c != current; c = c->prev) {
        if ((c->flags & WINDOW_FLAG_VISIBLE) &&
            !(c->flags & WINDOW_FLAG_CLOSING)) {
            return c;
        }
    }
    return current;
}

/**
 * Handle key event
 */
static void handle_key(key_event_t *key, bool pressed)
{
    /* Alt release commits an in-progress Alt+Tab cycle by raising the
     * currently selected window to the top of the z-order. */
    if (!pressed) {
        if ((key->keycode == KEY_LEFTALT || key->keycode == KEY_RIGHTALT)
                && wm.alt_tab_active) {
            wm.alt_tab_active = false;
            if (wm.focused) {
                wm_focus_window(wm.focused);
            }
        }
        return;
    }

    /* Alt+Tab — cycle focus without raising; commit happens on Alt up. */
    if (key->keycode == KEY_TAB && (key->modifiers & MOD_ALT)) {
        window_t *next = next_focus_candidate(wm.focused);
        wm.alt_tab_active = true;
        focus_no_raise(next);
        return;
    }

    /* Alt+F4 — request close on the focused window. Reuses the close-button
     * path: the window goes into the close fade and gets reaped a bit later. */
    if (key->keycode == KEY_F4 && (key->modifiers & MOD_ALT)) {
        if (wm.focused && !(wm.focused->flags & WINDOW_FLAG_CLOSING)) {
            wm.focused->flags |= WINDOW_FLAG_CLOSING;
            wm.focused->close_anim_start_ms = timer_get_uptime_ms();
            wm.needs_redraw = true;
        }
        return;
    }

    /* Esc — dismiss UI overlays (start menu, notifications) before the key
     * reaches the focused window. Apps that care about Esc still see it on
     * subsequent presses once nothing's left to dismiss. */
    if (key->keycode == KEY_ESCAPE) {
        bool consumed = false;
        if (desktop_start_menu_visible()) {
            desktop_dismiss_start_menu();
            consumed = true;
        }
        if (notify_active()) {
            notify_dismiss_all();
            consumed = true;
        }
        if (consumed) {
            wm.needs_redraw = true;
            return;
        }
    }

    /* Skip closing windows so the fade-out can't be interrupted */
    if (wm.focused && (wm.focused->flags & WINDOW_FLAG_CLOSING)) {
        return;
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
 * Reap windows whose close animation has completed. Calls the app-provided
 * on_close (which unregisters and frees), or falls back to default destroy.
 */
static void reap_closing_windows(void)
{
    uint64_t now = timer_get_uptime_ms();
    window_t *win = wm.window_list;
    window_t *next;

    while (win != NULL) {
        next = win->next;
        if ((win->flags & WINDOW_FLAG_CLOSING) &&
            (now - win->close_anim_start_ms) >= WINDOW_CLOSE_ANIM_MS) {
            /* Clear the flag first so on_close doesn't loop back through here */
            win->flags &= ~WINDOW_FLAG_CLOSING;
            wm.needs_redraw = true;
            if (win->on_close) {
                win->on_close(win);
            } else {
                wm_unregister_window(win);
                window_destroy(win);
            }
        }
        win = next;
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

        /* Reap any closing windows whose fade-out has completed */
        reap_closing_windows();

        /* Render at ~30 FPS. The frame tick forces a full content redraw so
         * live content advances WITHOUT an input event: the PL031 wall clock,
         * window open/close animations, and the sysmon graph. The old code only
         * re-rendered when an event had set needs_redraw, so between events the
         * loop re-pushed a stale framebuffer - the clock froze and a freshly
         * opened window sat on the first frame of its slide-in (the deep-blue
         * open overlay at full alpha) until a drag set the flag again. A
         * needs_redraw set by an event in between still updates immediately, so
         * clicks stay sub-frame responsive. */
        now = timer_get_uptime_ms();
        if (now - last_update >= 33) {
            wm.needs_redraw = true;
            last_update = now;
        }
        if (wm.needs_redraw) {
            wm_update_display();
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

void wm_request_redraw(void)
{
    wm.needs_redraw = true;
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
