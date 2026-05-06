/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/kernel/window.c
 * Description: Window management implementation
 * ============================================================================ */

#include <aeos/window.h>
#include <aeos/framebuffer.h>
#include <aeos/heap.h>
#include <aeos/string.h>
#include <aeos/kprintf.h>
#include <aeos/timer.h>
#include <aeos/anim.h>

/* Window ID counter */
static uint32_t next_window_id = 1;

/**
 * Update client area dimensions based on flags
 */
static void update_client_area(window_t *win)
{
    if (win->flags & WINDOW_FLAG_DECORATED) {
        win->client_x = win->x + WINDOW_BORDER_WIDTH;
        win->client_y = win->y + WINDOW_TITLE_HEIGHT;
        win->client_width = win->width - (2 * WINDOW_BORDER_WIDTH);
        win->client_height = win->height - WINDOW_TITLE_HEIGHT - WINDOW_BORDER_WIDTH;
    } else {
        win->client_x = win->x;
        win->client_y = win->y;
        win->client_width = win->width;
        win->client_height = win->height;
    }
}

/**
 * Create a new window
 */
window_t *window_create(const char *title, int32_t x, int32_t y,
                         uint32_t width, uint32_t height, uint32_t flags)
{
    window_t *win;

    win = (window_t *)kmalloc(sizeof(window_t));
    if (!win) {
        klog_error("Failed to allocate window");
        return NULL;
    }

    memset(win, 0, sizeof(window_t));

    /* Set properties */
    win->id = next_window_id++;
    if (title) {
        strncpy(win->title, title, WINDOW_TITLE_MAX - 1);
        win->title[WINDOW_TITLE_MAX - 1] = '\0';
    }

    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;

    /* Default to decorated windows */
    win->flags = flags | WINDOW_FLAG_DECORATED | WINDOW_FLAG_DIRTY;

    /* Calculate client area */
    update_client_area(win);

    /* No backbuffer by default (direct rendering) */
    win->backbuffer = NULL;
    win->backbuffer_size = 0;

    win->next = NULL;
    win->prev = NULL;

    klog_debug("Created window %u: '%s' at (%d,%d) %ux%u",
               win->id, win->title, x, y, width, height);

    return win;
}

/**
 * Destroy a window
 */
void window_destroy(window_t *win)
{
    if (!win) {
        return;
    }

    klog_debug("Destroying window %u: '%s'", win->id, win->title);

    /* Free backbuffer if allocated */
    if (win->backbuffer) {
        kfree(win->backbuffer);
    }

    kfree(win);
}

/**
 * Show a window
 */
void window_show(window_t *win)
{
    if (win) {
        win->flags |= WINDOW_FLAG_VISIBLE | WINDOW_FLAG_DIRTY;
    }
}

/**
 * Hide a window
 */
void window_hide(window_t *win)
{
    if (win) {
        win->flags &= ~WINDOW_FLAG_VISIBLE;
    }
}

/**
 * Set window title
 */
void window_set_title(window_t *win, const char *title)
{
    if (win && title) {
        strncpy(win->title, title, WINDOW_TITLE_MAX - 1);
        win->title[WINDOW_TITLE_MAX - 1] = '\0';
        win->flags |= WINDOW_FLAG_DIRTY;
    }
}

/**
 * Move window
 */
void window_move(window_t *win, int32_t x, int32_t y)
{
    if (win) {
        win->x = x;
        win->y = y;
        update_client_area(win);
        win->flags |= WINDOW_FLAG_DIRTY;
    }
}

/**
 * Resize window
 */
void window_resize(window_t *win, uint32_t width, uint32_t height)
{
    if (win) {
        win->width = width;
        win->height = height;
        update_client_area(win);
        win->flags |= WINDOW_FLAG_DIRTY;
    }
}

/**
 * Mark window as needing redraw
 */
void window_invalidate(window_t *win)
{
    if (win) {
        win->flags |= WINDOW_FLAG_DIRTY;
    }
}

/**
 * Draw window decorations
 * Safe with partially off-screen windows (negative x/y coordinates)
 */
void window_draw_decorations(window_t *win, bool focused)
{
    uint32_t title_bg;
    uint32_t title_fg;
    uint32_t border;
    uint32_t close_bg;
    int32_t close_x, close_y;
    fb_info_t *fb = fb_get_info();

    if (!win || !(win->flags & WINDOW_FLAG_DECORATED) || !fb || !fb->initialized) {
        return;
    }

    /* Skip entirely off-screen windows */
    if (win->x + (int32_t)win->width <= 0 || win->x >= (int32_t)fb->width ||
        win->y + (int32_t)win->height <= 0 || win->y >= (int32_t)fb->height) {
        return;
    }

    /* Title bar, title text, and border each pick a focus-aware color */
    if (focused) {
        title_bg = WINDOW_TITLE_BG_FOCUSED;
        title_fg = WINDOW_TITLE_FG_FOCUSED;
        border   = WINDOW_BORDER_FOCUSED;
    } else {
        title_bg = WINDOW_TITLE_BG_UNFOCUSED;
        title_fg = WINDOW_TITLE_FG_UNFOCUSED;
        border   = WINDOW_BORDER_UNFOCUSED;
    }

    /* Draw title bar — fb_fill_rect safely handles signed coordinates */
    fb_fill_rect(win->x, win->y, win->width, WINDOW_TITLE_HEIGHT, title_bg);

    /* Draw title text if visible */
    if (win->x + 8 >= 0 && win->y + WINDOW_TITLE_TEXT_Y >= 0) {
        fb_puts(win->x + 8, win->y + WINDOW_TITLE_TEXT_Y, win->title,
                title_fg, title_bg);
    }

    /* Draw close button if visible */
    close_x = win->x + win->width - WINDOW_CLOSE_BTN_SIZE - WINDOW_CLOSE_BTN_MARGIN;
    close_y = win->y + WINDOW_CLOSE_BTN_TOP;
    close_bg = WINDOW_CLOSE_BTN_BG;

    if (close_x >= 0 && close_y >= 0 &&
        close_x < (int32_t)fb->width && close_y < (int32_t)fb->height) {
        fb_fill_rect(close_x, close_y, WINDOW_CLOSE_BTN_SIZE, WINDOW_CLOSE_BTN_SIZE, close_bg);

        /* Draw X on close button — light glyph against the red */
        int32_t cx = close_x + WINDOW_CLOSE_BTN_SIZE / 2;
        int32_t cy = close_y + WINDOW_CLOSE_BTN_SIZE / 2;
        fb_draw_line(cx - 4, cy - 4, cx + 4, cy + 4, WINDOW_TITLE_FG);
        fb_draw_line(cx - 4, cy + 4, cx + 4, cy - 4, WINDOW_TITLE_FG);
    }

    /* Draw border — fb_draw_rect safely handles signed coordinates */
    fb_draw_rect(win->x, win->y, win->width, win->height, border);
}

/**
 * Draw drop shadow for focused windows (single-line fallback).
 */
void window_draw_shadow(window_t *win)
{
    if (!win || !(win->flags & WINDOW_FLAG_FOCUSED)) {
        return;
    }
    /* 2-px-tall dark band offset 1 px below the window. Cheap stand-in for
     * the three-rect alpha shadow described in DESIGN_SYSTEM.md, which needs
     * per-pixel alpha blending we don't yet have. */
    fb_fill_rect(win->x + 2,
                 win->y + (int32_t)win->height + 1,
                 win->width,
                 2,
                 THEME_BORDER_SUBTLE);
}

/**
 * Draw window (decorations + content). Applies open/close animation: a 4-px
 * slide-up plus a fade overlay over the window rect. Cubic ease-out, 180 ms
 * to open, 120 ms to close.
 */
void window_draw(window_t *win)
{
    bool focused;
    fb_info_t *fb = fb_get_info();
    uint64_t now;
    int32_t open_t, close_t, eased;
    int32_t slide_offset = 0;
    int32_t saved_y = 0, saved_client_y = 0;
    uint32_t fade_alpha = 0;
    bool slid = false;

    if (!win || !(win->flags & WINDOW_FLAG_VISIBLE)) {
        return;
    }

    /* Skip entirely off-screen windows */
    if (fb && fb->initialized) {
        if (win->x + (int32_t)win->width <= 0 || win->x >= (int32_t)fb->width ||
            win->y + (int32_t)win->height <= 0 || win->y >= (int32_t)fb->height) {
            win->flags &= ~WINDOW_FLAG_DIRTY;
            return;
        }
    }

    focused = (win->flags & WINDOW_FLAG_FOCUSED) != 0;
    now = timer_get_uptime_ms();

    /* Open animation: slide-up + fade-in. Once eased reaches ANIM_Q8_ONE the
     * draw is identical to a non-animated window. */
    open_t = anim_progress_q8(now, win->open_anim_start_ms, WINDOW_OPEN_ANIM_MS);
    if (win->open_anim_start_ms != 0 && open_t < ANIM_Q8_ONE) {
        eased = ease_out_cubic_q8(open_t);
        slide_offset = WINDOW_OPEN_SLIDE_PX -
                       ((WINDOW_OPEN_SLIDE_PX * eased) >> 8);
        fade_alpha = (uint32_t)(ANIM_Q8_ONE - eased);
    }

    /* Close animation: fade-out only. WM keeps the window flagged CLOSING and
     * reaps it once the duration elapses. */
    if (win->flags & WINDOW_FLAG_CLOSING) {
        close_t = anim_progress_q8(now, win->close_anim_start_ms,
                                    WINDOW_CLOSE_ANIM_MS);
        eased = ease_out_cubic_q8(close_t);
        fade_alpha = (uint32_t)eased;  /* opaque -> bg as we close */
    }

    /* Apply slide offset to the draw position only; hit-testing keeps the
     * unanimated coordinates so events still land where the user clicked. */
    if (slide_offset > 0) {
        saved_y = win->y;
        saved_client_y = win->client_y;
        win->y += slide_offset;
        win->client_y += slide_offset;
        slid = true;
    }

    /* Drop shadow goes under the decorations of focused windows */
    if (focused) {
        window_draw_shadow(win);
    }

    /* Draw decorations */
    window_draw_decorations(win, focused);

    /* Fill client area with background — fb_fill_rect handles signed coords */
    fb_fill_rect(win->client_x, win->client_y,
                 win->client_width, win->client_height,
                 WINDOW_CLIENT_BG);

    /* Call paint callback if set */
    if (win->on_paint) {
        win->on_paint(win);
    }

    /* Fade overlay: blend the bg color over the whole window rect at fade_alpha */
    if (fade_alpha != 0) {
        fb_blend_rect(win->x, win->y, (int32_t)win->width, (int32_t)win->height,
                      THEME_BG_DEEP, fade_alpha);
    }

    /* Restore original positions for hit-testing */
    if (slid) {
        win->y = saved_y;
        win->client_y = saved_client_y;
    }

    /* Clear dirty flag */
    win->flags &= ~WINDOW_FLAG_DIRTY;
}

/**
 * Check if point is in window
 */
bool window_contains_point(window_t *win, int32_t x, int32_t y)
{
    if (!win || !(win->flags & WINDOW_FLAG_VISIBLE)) {
        return false;
    }

    return (x >= win->x && x < win->x + (int32_t)win->width &&
            y >= win->y && y < win->y + (int32_t)win->height);
}

/**
 * Check if point is in title bar
 */
bool window_in_title_bar(window_t *win, int32_t x, int32_t y)
{
    if (!win || !(win->flags & WINDOW_FLAG_DECORATED)) {
        return false;
    }

    return (x >= win->x && x < win->x + (int32_t)win->width &&
            y >= win->y && y < win->y + WINDOW_TITLE_HEIGHT);
}

/**
 * Check if point is on close button
 */
bool window_in_close_button(window_t *win, int32_t x, int32_t y)
{
    int32_t close_x, close_y;

    if (!win || !(win->flags & WINDOW_FLAG_DECORATED)) {
        return false;
    }

    close_x = win->x + win->width - WINDOW_CLOSE_BTN_SIZE - WINDOW_CLOSE_BTN_MARGIN;
    close_y = win->y + WINDOW_CLOSE_BTN_TOP;

    return (x >= close_x && x < close_x + WINDOW_CLOSE_BTN_SIZE &&
            y >= close_y && y < close_y + WINDOW_CLOSE_BTN_SIZE);
}

/**
 * Clear window client area
 */
void window_clear(window_t *win, uint32_t color)
{
    if (!win) {
        return;
    }

    fb_fill_rect(win->client_x, win->client_y,
                 win->client_width, win->client_height, color);
}

/**
 * Draw pixel in client area
 */
void window_putpixel(window_t *win, int32_t x, int32_t y, uint32_t color)
{
    int32_t abs_x, abs_y;

    if (!win) {
        return;
    }

    /* Convert to screen coordinates */
    abs_x = win->client_x + x;
    abs_y = win->client_y + y;

    /* Clip to client area */
    if (x < 0 || y < 0 ||
        x >= (int32_t)win->client_width ||
        y >= (int32_t)win->client_height) {
        return;
    }

    fb_putpixel(abs_x, abs_y, color);
}

/**
 * Draw filled rectangle in client area
 */
void window_fill_rect(window_t *win, int32_t x, int32_t y,
                       uint32_t w, uint32_t h, uint32_t color)
{
    int32_t abs_x, abs_y;

    if (!win) {
        return;
    }

    /* Convert to screen coordinates */
    abs_x = win->client_x + x;
    abs_y = win->client_y + y;

    /* Clip to client area */
    if (abs_x < win->client_x) {
        w -= (win->client_x - abs_x);
        abs_x = win->client_x;
    }
    if (abs_y < win->client_y) {
        h -= (win->client_y - abs_y);
        abs_y = win->client_y;
    }
    if (abs_x + (int32_t)w > win->client_x + (int32_t)win->client_width) {
        w = win->client_x + win->client_width - abs_x;
    }
    if (abs_y + (int32_t)h > win->client_y + (int32_t)win->client_height) {
        h = win->client_y + win->client_height - abs_y;
    }

    if ((int32_t)w > 0 && (int32_t)h > 0) {
        fb_fill_rect(abs_x, abs_y, w, h, color);
    }
}

/**
 * Draw rectangle outline in client area
 */
void window_draw_rect(window_t *win, int32_t x, int32_t y,
                       uint32_t w, uint32_t h, uint32_t color)
{
    int32_t abs_x, abs_y;

    if (!win) {
        return;
    }

    abs_x = win->client_x + x;
    abs_y = win->client_y + y;

    fb_draw_rect(abs_x, abs_y, w, h, color);
}

/**
 * Draw text in client area
 */
void window_puts(window_t *win, int32_t x, int32_t y,
                  const char *text, uint32_t fg, uint32_t bg)
{
    int32_t abs_x, abs_y;

    if (!win || !text) {
        return;
    }

    abs_x = win->client_x + x;
    abs_y = win->client_y + y;

    /* Check bounds */
    if (y < 0 || y >= (int32_t)win->client_height) {
        return;
    }

    fb_puts(abs_x, abs_y, text, fg, bg);
}

/**
 * Draw single character in client area
 */
void window_putchar(window_t *win, int32_t x, int32_t y,
                     char c, uint32_t fg, uint32_t bg)
{
    int32_t abs_x, abs_y;

    if (!win) {
        return;
    }

    abs_x = win->client_x + x;
    abs_y = win->client_y + y;

    /* Clip to client area */
    if (x < 0 || y < 0 ||
        x + 8 > (int32_t)win->client_width ||
        y + 8 > (int32_t)win->client_height) {
        return;
    }

    fb_putchar(abs_x, abs_y, c, fg, bg);
}

/**
 * Draw text in client area using the 8x16 font.
 */
void window_puts_large(window_t *win, int32_t x, int32_t y,
                        const char *text, uint32_t fg, uint32_t bg)
{
    int32_t abs_x, abs_y;

    if (!win || !text) {
        return;
    }

    abs_x = win->client_x + x;
    abs_y = win->client_y + y;

    if (y < 0 || y >= (int32_t)win->client_height) {
        return;
    }

    fb_puts_large(abs_x, abs_y, text, fg, bg);
}

/**
 * Draw single character in client area using the 8x16 font.
 */
void window_putchar_large(window_t *win, int32_t x, int32_t y,
                           char c, uint32_t fg, uint32_t bg)
{
    int32_t abs_x, abs_y;

    if (!win) {
        return;
    }

    abs_x = win->client_x + x;
    abs_y = win->client_y + y;

    if (x < 0 || y < 0 ||
        x + 8 > (int32_t)win->client_width ||
        y + 16 > (int32_t)win->client_height) {
        return;
    }

    fb_putchar_large(abs_x, abs_y, c, fg, bg);
}

/**
 * Draw line in client area
 */
void window_draw_line(window_t *win, int32_t x1, int32_t y1,
                       int32_t x2, int32_t y2, uint32_t color)
{
    if (!win) {
        return;
    }

    /* Convert to screen coordinates */
    fb_draw_line(win->client_x + x1, win->client_y + y1,
                 win->client_x + x2, win->client_y + y2, color);
}

/* ============================================================================
 * End of window.c
 * ============================================================================ */
