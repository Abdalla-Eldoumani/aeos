/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/apps/about.c
 * Description: About dialog
 * ============================================================================ */

#include <aeos/apps/about.h>
#include <aeos/window.h>
#include <aeos/wm.h>
#include <aeos/heap.h>
#include <aeos/string.h>
#include <aeos/kprintf.h>
#include <aeos/theme.h>

/* Colors */
#define ABOUT_BG            THEME_SURFACE_1
#define ABOUT_LOGO_COLOR    THEME_ACCENT
#define ABOUT_TEXT          THEME_TEXT_PRIMARY
#define ABOUT_HIGHLIGHT     THEME_TEXT_PRIMARY
#define ABOUT_DIM           THEME_TEXT_SECONDARY

/* Forward declarations */
static void about_paint(window_t *win);
static void about_close(window_t *win);

/**
 * Draw simple logo
 */
static void draw_logo(window_t *win, int32_t x, int32_t y)
{
    /* Draw "AEOS" in a box */
    window_fill_rect(win, x, y, 120, 60, THEME_SURFACE_2);
    window_draw_rect(win, x, y, 120, 60, ABOUT_LOGO_COLOR);
    window_draw_rect(win, x + 1, y + 1, 118, 58, ABOUT_LOGO_COLOR);

    /* Draw text */
    window_puts(win, x + 20, y + 12, "A E O S", ABOUT_LOGO_COLOR, THEME_SURFACE_2);
    window_puts(win, x + 20, y + 22, "A E O S", ABOUT_LOGO_COLOR, THEME_SURFACE_2);

    /* Decorative line */
    window_fill_rect(win, x + 20, y + 38, 80, 2, ABOUT_LOGO_COLOR);

    /* Version */
    window_puts(win, x + 32, y + 44, "v1.0", ABOUT_TEXT, THEME_SURFACE_2);
}

/**
 * Create about window
 */
about_t *about_create(void)
{
    about_t *about;

    about = (about_t *)kmalloc(sizeof(about_t));
    if (!about) {
        return NULL;
    }

    memset(about, 0, sizeof(about_t));

    /* Create window (smaller dialog) */
    about->window = window_create("About AEOS", 220, 120, 280, 220,
                                   WINDOW_FLAG_VISIBLE);
    if (!about->window) {
        kfree(about);
        return NULL;
    }

    /* Set callbacks */
    about->window->on_paint = about_paint;
    about->window->on_close = about_close;
    about->window->user_data = about;

    /* Register with window manager */
    wm_register_window(about->window);

    return about;
}

/**
 * Destroy about
 */
void about_destroy(about_t *about)
{
    if (!about) {
        return;
    }

    kfree(about);
}

/**
 * Draw about content
 */
static void about_paint(window_t *win)
{
    int32_t y;
    int32_t center_x = (win->client_width - 120) / 2;

    /* Clear background */
    window_clear(win, ABOUT_BG);

    /* Draw logo centered */
    draw_logo(win, center_x, 10);

    y = 80;

    /* Title */
    window_puts_large(win, 30, y, "Abdalla's Educational", ABOUT_HIGHLIGHT, ABOUT_BG);
    y += 18;
    window_puts_large(win, 50, y, "Operating System", ABOUT_HIGHLIGHT, ABOUT_BG);
    y += 24;

    /* Info */
    window_puts(win, 50, y, "Architecture:", ABOUT_DIM, ABOUT_BG);
    window_puts(win, 150, y, "AArch64", ABOUT_TEXT, ABOUT_BG);
    y += 14;

    window_puts(win, 50, y, "Platform:", ABOUT_DIM, ABOUT_BG);
    window_puts(win, 150, y, "QEMU virt", ABOUT_TEXT, ABOUT_BG);
    y += 14;

    window_puts(win, 50, y, "Graphics:", ABOUT_DIM, ABOUT_BG);
    window_puts(win, 150, y, "VirtIO GPU", ABOUT_TEXT, ABOUT_BG);
    y += 20;

    /* Footer */
    window_puts(win, 60, y, "Built for learning!", ABOUT_LOGO_COLOR, ABOUT_BG);
}

/**
 * Handle close
 */
static void about_close(window_t *win)
{
    about_t *about = (about_t *)win->user_data;

    /* Null callbacks before destroy so any in-flight event finds NULL */
    win->on_paint = NULL;
    win->on_key = NULL;
    win->on_mouse = NULL;
    win->on_close = NULL;

    wm_unregister_window(win);
    window_destroy(win);

    if (about) {
        kfree(about);
    }
}

/* ============================================================================
 * End of about.c
 * ============================================================================ */
