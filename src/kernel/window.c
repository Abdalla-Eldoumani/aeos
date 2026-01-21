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
 */
void window_draw_decorations(window_t *win, bool focused)
{
    uint32_t title_bg;
    uint32_t close_bg;
    int32_t close_x, close_y;

    if (!win || !(win->flags & WINDOW_FLAG_DECORATED)) {
        return;
    }

    /* Title bar color based on focus */
    title_bg = focused ? WINDOW_TITLE_BG_FOCUSED : WINDOW_TITLE_BG_UNFOCUSED;

    /* Draw title bar */
    fb_fill_rect(win->x, win->y, win->width, WINDOW_TITLE_HEIGHT, title_bg);

    /* Draw title text (centered vertically) */
    fb_puts(win->x + 8, win->y + 6, win->title, WINDOW_TITLE_FG, title_bg);

    /* Draw close button */
    close_x = win->x + win->width - WINDOW_CLOSE_BTN_SIZE - 2;
    close_y = win->y + 2;
    close_bg = WINDOW_CLOSE_BTN_BG;

    fb_fill_rect(close_x, close_y, WINDOW_CLOSE_BTN_SIZE, WINDOW_CLOSE_BTN_SIZE, close_bg);

    /* Draw X on close button */
    int32_t cx = close_x + WINDOW_CLOSE_BTN_SIZE / 2;
    int32_t cy = close_y + WINDOW_CLOSE_BTN_SIZE / 2;
    fb_draw_line(cx - 4, cy - 4, cx + 4, cy + 4, WINDOW_TITLE_FG);
    fb_draw_line(cx - 4, cy + 4, cx + 4, cy - 4, WINDOW_TITLE_FG);

    /* Draw border */
    fb_draw_rect(win->x, win->y, win->width, win->height, WINDOW_BORDER_COLOR);
}

/**
 * Draw window (decorations + content)
 */
void window_draw(window_t *win)
{
    bool focused;

    if (!win || !(win->flags & WINDOW_FLAG_VISIBLE)) {
        return;
    }

    focused = (win->flags & WINDOW_FLAG_FOCUSED) != 0;

    /* Draw decorations */
    window_draw_decorations(win, focused);

    /* Fill client area with background */
    fb_fill_rect(win->client_x, win->client_y,
                 win->client_width, win->client_height,
                 WINDOW_CLIENT_BG);

    /* Call paint callback if set */
    if (win->on_paint) {
        win->on_paint(win);
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

    close_x = win->x + win->width - WINDOW_CLOSE_BTN_SIZE - 2;
    close_y = win->y + 2;

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
