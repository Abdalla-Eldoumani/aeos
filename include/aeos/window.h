/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/window.h
 * Description: Window structure and management
 * ============================================================================ */

#ifndef AEOS_WINDOW_H
#define AEOS_WINDOW_H

#include <aeos/types.h>
#include <aeos/event.h>

/* Maximum windows */
#define MAX_WINDOWS 16

/* Window title maximum length */
#define WINDOW_TITLE_MAX 64

/* Window decoration sizes */
#define WINDOW_TITLE_HEIGHT 20
#define WINDOW_BORDER_WIDTH 1
#define WINDOW_CLOSE_BTN_SIZE 16

/* Window flags */
#define WINDOW_FLAG_VISIBLE     (1 << 0)
#define WINDOW_FLAG_FOCUSED     (1 << 1)
#define WINDOW_FLAG_DRAGGING    (1 << 2)
#define WINDOW_FLAG_RESIZING    (1 << 3)
#define WINDOW_FLAG_MINIMIZED   (1 << 4)
#define WINDOW_FLAG_MAXIMIZED   (1 << 5)
#define WINDOW_FLAG_DECORATED   (1 << 6)  /* Has title bar and border */
#define WINDOW_FLAG_DIRTY       (1 << 7)  /* Needs redraw */

/* Window colors */
#define WINDOW_TITLE_BG_FOCUSED     0xFF2060A0  /* Blue title bar */
#define WINDOW_TITLE_BG_UNFOCUSED   0xFF404050  /* Gray title bar */
#define WINDOW_TITLE_FG             0xFFFFFFFF  /* White text */
#define WINDOW_BORDER_COLOR         0xFF303040  /* Dark border */
#define WINDOW_CLIENT_BG            0xFF1A1A2E  /* Dark client area */
#define WINDOW_CLOSE_BTN_BG         0xFFC04040  /* Red close button */
#define WINDOW_CLOSE_BTN_HOVER      0xFFFF4040  /* Brighter red on hover */

/* Forward declaration */
struct window;

/* Window event callback types */
typedef void (*window_paint_fn)(struct window *win);
typedef void (*window_key_fn)(struct window *win, key_event_t *key);
typedef void (*window_mouse_fn)(struct window *win, mouse_event_t *mouse);
typedef void (*window_close_fn)(struct window *win);

/* Window structure */
typedef struct window {
    /* Identification */
    uint32_t id;
    char title[WINDOW_TITLE_MAX];

    /* Position and size (including decorations) */
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;

    /* Client area (content area inside decorations) */
    int32_t client_x;
    int32_t client_y;
    uint32_t client_width;
    uint32_t client_height;

    /* Backbuffer (optional - for double buffering) */
    uint32_t *backbuffer;
    uint32_t backbuffer_size;

    /* State */
    uint32_t flags;

    /* Z-order (higher = on top) */
    int32_t z_order;

    /* Dragging state */
    int32_t drag_offset_x;
    int32_t drag_offset_y;

    /* Callbacks */
    window_paint_fn on_paint;
    window_key_fn on_key;
    window_mouse_fn on_mouse;
    window_close_fn on_close;

    /* User data */
    void *user_data;

    /* Linked list for window manager */
    struct window *next;
    struct window *prev;
} window_t;

/**
 * Create a new window
 * @param title Window title
 * @param x X position
 * @param y Y position
 * @param width Width (including decorations)
 * @param height Height (including decorations)
 * @param flags Initial flags
 * @return Window pointer or NULL on error
 */
window_t *window_create(const char *title, int32_t x, int32_t y,
                         uint32_t width, uint32_t height, uint32_t flags);

/**
 * Destroy a window
 */
void window_destroy(window_t *win);

/**
 * Show a window
 */
void window_show(window_t *win);

/**
 * Hide a window
 */
void window_hide(window_t *win);

/**
 * Set window title
 */
void window_set_title(window_t *win, const char *title);

/**
 * Move window to new position
 */
void window_move(window_t *win, int32_t x, int32_t y);

/**
 * Resize window
 */
void window_resize(window_t *win, uint32_t width, uint32_t height);

/**
 * Mark window as needing redraw
 */
void window_invalidate(window_t *win);

/**
 * Draw window (decorations + content)
 */
void window_draw(window_t *win);

/**
 * Draw window decorations (title bar, border, close button)
 */
void window_draw_decorations(window_t *win, bool focused);

/**
 * Check if point is in window
 */
bool window_contains_point(window_t *win, int32_t x, int32_t y);

/**
 * Check if point is in title bar
 */
bool window_in_title_bar(window_t *win, int32_t x, int32_t y);

/**
 * Check if point is on close button
 */
bool window_in_close_button(window_t *win, int32_t x, int32_t y);

/**
 * Clear window client area with color
 */
void window_clear(window_t *win, uint32_t color);

/**
 * Draw pixel in window client area
 */
void window_putpixel(window_t *win, int32_t x, int32_t y, uint32_t color);

/**
 * Draw filled rectangle in window client area
 */
void window_fill_rect(window_t *win, int32_t x, int32_t y,
                       uint32_t w, uint32_t h, uint32_t color);

/**
 * Draw rectangle outline in window client area
 */
void window_draw_rect(window_t *win, int32_t x, int32_t y,
                       uint32_t w, uint32_t h, uint32_t color);

/**
 * Draw text in window client area
 */
void window_puts(window_t *win, int32_t x, int32_t y,
                  const char *text, uint32_t fg, uint32_t bg);

/**
 * Draw single character in window client area
 */
void window_putchar(window_t *win, int32_t x, int32_t y,
                     char c, uint32_t fg, uint32_t bg);

/**
 * Draw line in window client area
 */
void window_draw_line(window_t *win, int32_t x1, int32_t y1,
                       int32_t x2, int32_t y2, uint32_t color);

#endif /* AEOS_WINDOW_H */
