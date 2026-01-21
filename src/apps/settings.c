/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/apps/settings.c
 * Description: Settings application
 * ============================================================================ */

#include <aeos/apps/settings.h>
#include <aeos/window.h>
#include <aeos/wm.h>
#include <aeos/heap.h>
#include <aeos/timer.h>
#include <aeos/process.h>
#include <aeos/string.h>
#include <aeos/kprintf.h>

/* Colors */
#define SETTINGS_BG         0xFF1A1A2E
#define SETTINGS_SECTION_BG 0xFF252540
#define SETTINGS_LABEL      0xFF888888
#define SETTINGS_VALUE      0xFFFFFFFF
#define SETTINGS_HEADER     0xFF00AAFF

/* Forward declarations */
static void settings_paint(window_t *win);
static void settings_close(window_t *win);

/**
 * Format memory size
 */
static void format_size(char *buf, size_t size, uint64_t bytes)
{
    if (bytes >= 1024 * 1024) {
        snprintf(buf, size, "%u MB", (uint32_t)(bytes / (1024 * 1024)));
    } else if (bytes >= 1024) {
        snprintf(buf, size, "%u KB", (uint32_t)(bytes / 1024));
    } else {
        snprintf(buf, size, "%u B", (uint32_t)bytes);
    }
}

/**
 * Create settings window
 */
settings_t *settings_create(void)
{
    settings_t *settings;

    settings = (settings_t *)kmalloc(sizeof(settings_t));
    if (!settings) {
        return NULL;
    }

    memset(settings, 0, sizeof(settings_t));

    /* Create window */
    settings->window = window_create("Settings", 200, 100, 350, 280,
                                      WINDOW_FLAG_VISIBLE);
    if (!settings->window) {
        kfree(settings);
        return NULL;
    }

    /* Set callbacks */
    settings->window->on_paint = settings_paint;
    settings->window->on_close = settings_close;
    settings->window->user_data = settings;

    /* Register with window manager */
    wm_register_window(settings->window);

    return settings;
}

/**
 * Destroy settings
 */
void settings_destroy(settings_t *settings)
{
    if (!settings) {
        return;
    }

    kfree(settings);
}

/**
 * Draw settings content
 */
static void settings_paint(window_t *win)
{
    heap_stats_t heap_stats;
    char buf[64];
    int32_t y = 10;
    uint64_t uptime_sec;
    uint32_t hours, minutes, seconds;

    /* Clear background */
    window_clear(win, SETTINGS_BG);

    /* Title */
    window_puts(win, 10, y, "System Information", SETTINGS_HEADER, SETTINGS_BG);
    y += 24;

    /* Memory section */
    window_fill_rect(win, 10, y, win->client_width - 20, 80, SETTINGS_SECTION_BG);
    y += 8;

    window_puts(win, 20, y, "Memory", SETTINGS_HEADER, SETTINGS_SECTION_BG);
    y += 16;

    heap_get_stats(&heap_stats);

    window_puts(win, 20, y, "Used:", SETTINGS_LABEL, SETTINGS_SECTION_BG);
    format_size(buf, sizeof(buf), heap_stats.used_size);
    window_puts(win, 120, y, buf, SETTINGS_VALUE, SETTINGS_SECTION_BG);
    y += 14;

    window_puts(win, 20, y, "Free:", SETTINGS_LABEL, SETTINGS_SECTION_BG);
    format_size(buf, sizeof(buf), heap_stats.free_size);
    window_puts(win, 120, y, buf, SETTINGS_VALUE, SETTINGS_SECTION_BG);
    y += 14;

    window_puts(win, 20, y, "Total:", SETTINGS_LABEL, SETTINGS_SECTION_BG);
    format_size(buf, sizeof(buf), heap_stats.used_size + heap_stats.free_size);
    window_puts(win, 120, y, buf, SETTINGS_VALUE, SETTINGS_SECTION_BG);
    y += 24;

    /* System section */
    window_fill_rect(win, 10, y, win->client_width - 20, 80, SETTINGS_SECTION_BG);
    y += 8;

    window_puts(win, 20, y, "System", SETTINGS_HEADER, SETTINGS_SECTION_BG);
    y += 16;

    window_puts(win, 20, y, "Arch:", SETTINGS_LABEL, SETTINGS_SECTION_BG);
    window_puts(win, 120, y, "ARMv8-A AArch64", SETTINGS_VALUE, SETTINGS_SECTION_BG);
    y += 14;

    window_puts(win, 20, y, "CPU:", SETTINGS_LABEL, SETTINGS_SECTION_BG);
    window_puts(win, 120, y, "Cortex-A57", SETTINGS_VALUE, SETTINGS_SECTION_BG);
    y += 14;

    uptime_sec = timer_get_uptime_sec();
    hours = uptime_sec / 3600;
    minutes = (uptime_sec % 3600) / 60;
    seconds = uptime_sec % 60;

    window_puts(win, 20, y, "Uptime:", SETTINGS_LABEL, SETTINGS_SECTION_BG);
    snprintf(buf, sizeof(buf), "%u:%02u:%02u", hours, minutes, seconds);
    window_puts(win, 120, y, buf, SETTINGS_VALUE, SETTINGS_SECTION_BG);
    y += 24;

    /* Display section */
    window_fill_rect(win, 10, y, win->client_width - 20, 50, SETTINGS_SECTION_BG);
    y += 8;

    window_puts(win, 20, y, "Display", SETTINGS_HEADER, SETTINGS_SECTION_BG);
    y += 16;

    window_puts(win, 20, y, "Resolution:", SETTINGS_LABEL, SETTINGS_SECTION_BG);
    window_puts(win, 120, y, "640x480 @ 32bpp", SETTINGS_VALUE, SETTINGS_SECTION_BG);
}

/**
 * Handle close
 */
static void settings_close(window_t *win)
{
    settings_t *settings = (settings_t *)win->user_data;

    wm_unregister_window(win);
    window_destroy(win);

    if (settings) {
        kfree(settings);
    }
}

/* ============================================================================
 * End of settings.c
 * ============================================================================ */
