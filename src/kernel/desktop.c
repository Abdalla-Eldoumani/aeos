/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/kernel/desktop.c
 * Description: Desktop environment implementation
 * ============================================================================ */

#include <aeos/desktop.h>
#include <aeos/framebuffer.h>
#include <aeos/wm.h>
#include <aeos/timer.h>
#include <aeos/kprintf.h>
#include <aeos/string.h>
#include <aeos/gui.h>
#include <aeos/theme.h>

/* Color scheme. Hover and selected variants reuse the same theme token until
 * Phase 3 redoes decorations. ICON_SELECTED_BG and ICON_LABEL_BG are alpha
 * overlays that the framebuffer draws as opaque today; keep them as one-offs. */
#define DESKTOP_BG_TOP      THEME_BG_GRADIENT_TOP
#define DESKTOP_BG_BOTTOM   THEME_BG_GRADIENT_BOT
#define TASKBAR_BG          THEME_SURFACE_1
#define TASKBAR_BORDER      THEME_BORDER_SUBTLE
#define START_BTN_BG        THEME_SUCCESS
#define START_BTN_HOVER     THEME_SUCCESS
#define ICON_SELECTED_BG    0x4000aaff  /* Semi-transparent blue */
#define ICON_LABEL_BG       0xC0000000  /* Semi-transparent black */
#define CLOCK_COLOR         THEME_TEXT_PRIMARY
#define TASKBAR_BTN_BG      THEME_SURFACE_2
#define TASKBAR_BTN_ACTIVE  THEME_SURFACE_3
#define TASKBAR_BTN_BORDER  THEME_BORDER_SUBTLE

/* Desktop state */
static struct {
    desktop_icon_t icons[MAX_DESKTOP_ICONS];
    uint32_t icon_count;
    int32_t selected_icon;
    bool start_menu_visible;
    bool initialized;
    uint64_t last_click_time;
    int32_t last_click_icon;
} desktop;

/**
 * Draw a simple icon (geometric shape)
 */
static void draw_icon_shape(int32_t x, int32_t y, uint32_t color, int type)
{
    int32_t cx = x + ICON_WIDTH / 2;
    int32_t cy = y + ICON_HEIGHT / 2;

    switch (type) {
        case 0:  /* Terminal - rectangle with > prompt */
            fb_fill_rect(x + 4, y + 4, ICON_WIDTH - 8, ICON_HEIGHT - 8, color);
            fb_fill_rect(x + 8, y + 8, ICON_WIDTH - 16, ICON_HEIGHT - 16, 0xFF000000);
            fb_puts(x + 12, y + 16, ">_", 0xFF00FF00, 0xFF000000);
            break;

        case 1:  /* Folder - folder shape */
            fb_fill_rect(x + 4, y + 12, ICON_WIDTH - 8, ICON_HEIGHT - 16, color);
            fb_fill_rect(x + 4, y + 8, 20, 8, color);
            fb_fill_rect(x + 6, y + 14, ICON_WIDTH - 12, ICON_HEIGHT - 22, 0xFF000000);
            break;

        case 2:  /* Settings - gear (simplified as circle with notches) */
            fb_fill_rect(cx - 16, cy - 4, 32, 8, color);
            fb_fill_rect(cx - 4, cy - 16, 8, 32, color);
            fb_fill_rect(cx - 12, cy - 12, 24, 24, color);
            fb_fill_rect(cx - 8, cy - 8, 16, 16, DESKTOP_BG_TOP);
            break;

        case 3:  /* Info/About - circle with i */
            fb_fill_rect(cx - 16, cy - 16, 32, 32, color);
            fb_fill_rect(cx - 2, cy - 10, 4, 4, 0xFFFFFFFF);
            fb_fill_rect(cx - 2, cy - 2, 4, 12, 0xFFFFFFFF);
            break;

        default:  /* Default - filled square */
            fb_fill_rect(x + 4, y + 4, ICON_WIDTH - 8, ICON_HEIGHT - 8, color);
            break;
    }
}

/**
 * Draw a desktop icon
 */
static void draw_desktop_icon(desktop_icon_t *icon, int type)
{
    uint32_t label_width;
    int32_t label_x;

    /* Selection highlight */
    if (icon->selected) {
        fb_fill_rect(icon->x - 4, icon->y - 4,
                     ICON_WIDTH + 8, ICON_HEIGHT + ICON_LABEL_HEIGHT + 12,
                     ICON_SELECTED_BG);
    }

    /* Draw icon shape */
    draw_icon_shape(icon->x, icon->y, icon->icon_color, type);

    /* Draw label */
    label_width = strlen(icon->name) * 8;
    label_x = icon->x + (ICON_WIDTH - label_width) / 2;

    /* Label background */
    fb_fill_rect(label_x - 2, icon->y + ICON_HEIGHT + 2,
                 label_width + 4, 12, ICON_LABEL_BG);

    /* Label text */
    fb_puts(label_x, icon->y + ICON_HEIGHT + 4,
            icon->name, THEME_TEXT_PRIMARY, ICON_LABEL_BG);
}

/**
 * Initialize desktop
 */
void desktop_init(void)
{
    klog_info("Initializing desktop environment...");

    memset(&desktop, 0, sizeof(desktop));
    desktop.selected_icon = -1;
    desktop.start_menu_visible = false;
    desktop.initialized = true;
    desktop.last_click_time = 0;
    desktop.last_click_icon = -1;

    klog_info("Desktop environment initialized");
}

/**
 * Draw desktop background (gradient)
 */
void desktop_draw_background(void)
{
    uint32_t y;
    uint32_t height = FB_HEIGHT - TASKBAR_HEIGHT;

    /* Draw vertical gradient */
    for (y = 0; y < height; y++) {
        /* Interpolate between top and bottom colors */
        uint32_t r1 = (DESKTOP_BG_TOP >> 16) & 0xFF;
        uint32_t g1 = (DESKTOP_BG_TOP >> 8) & 0xFF;
        uint32_t b1 = DESKTOP_BG_TOP & 0xFF;

        uint32_t r2 = (DESKTOP_BG_BOTTOM >> 16) & 0xFF;
        uint32_t g2 = (DESKTOP_BG_BOTTOM >> 8) & 0xFF;
        uint32_t b2 = DESKTOP_BG_BOTTOM & 0xFF;

        uint32_t r = r1 + (r2 - r1) * y / height;
        uint32_t g = g1 + (g2 - g1) * y / height;
        uint32_t b = b1 + (b2 - b1) * y / height;

        uint32_t color = 0xFF000000 | (r << 16) | (g << 8) | b;

        fb_fill_rect(0, y, FB_WIDTH, 1, color);
    }
}

/**
 * Draw desktop icons
 */
void desktop_draw_icons(void)
{
    uint32_t i;

    for (i = 0; i < desktop.icon_count; i++) {
        draw_desktop_icon(&desktop.icons[i], i);
    }
}

/**
 * Draw taskbar
 */
void desktop_draw_taskbar(void)
{
    uint32_t taskbar_y = FB_HEIGHT - TASKBAR_HEIGHT;
    window_t *win;
    int32_t btn_x;
    char time_str[16];
    uint64_t uptime_sec;
    uint32_t hours, minutes;

    /* Taskbar background */
    fb_fill_rect(0, taskbar_y, FB_WIDTH, TASKBAR_HEIGHT, TASKBAR_BG);

    /* Top border */
    fb_fill_rect(0, taskbar_y, FB_WIDTH, 1, TASKBAR_BORDER);

    /* Start button */
    fb_fill_rect(4, taskbar_y + 4, START_BUTTON_WIDTH, TASKBAR_HEIGHT - 8, START_BTN_BG);
    fb_puts(12, taskbar_y + 10, "AEOS", THEME_TEXT_PRIMARY, START_BTN_BG);

    /* Window buttons */
    btn_x = START_BUTTON_WIDTH + 12;
    for (win = wm_get_window_list(); win != NULL; win = win->next) {
        if (!(win->flags & WINDOW_FLAG_VISIBLE)) {
            continue;
        }

        uint32_t btn_bg = (win->flags & WINDOW_FLAG_FOCUSED) ?
                          TASKBAR_BTN_ACTIVE : TASKBAR_BTN_BG;

        /* Button background */
        fb_fill_rect(btn_x, taskbar_y + 4,
                     TASKBAR_BUTTON_WIDTH, TASKBAR_HEIGHT - 8, btn_bg);
        fb_draw_rect(btn_x, taskbar_y + 4,
                     TASKBAR_BUTTON_WIDTH, TASKBAR_HEIGHT - 8, TASKBAR_BTN_BORDER);

        /* Truncate title if needed */
        char short_title[16];
        strncpy(short_title, win->title, 14);
        short_title[14] = '\0';
        if (strlen(win->title) > 14) {
            short_title[13] = '.';
            short_title[14] = '.';
            short_title[15] = '\0';
        }

        fb_puts(btn_x + 8, taskbar_y + 10, short_title, THEME_TEXT_PRIMARY, btn_bg);

        btn_x += TASKBAR_BUTTON_WIDTH + 4;

        /* Stop if we run out of space */
        if (btn_x > FB_WIDTH - 100) {
            break;
        }
    }

    /* Clock (using uptime) */
    uptime_sec = timer_get_uptime_sec();
    hours = (uptime_sec / 3600) % 24;
    minutes = (uptime_sec / 60) % 60;

    /* Format time string */
    time_str[0] = '0' + (hours / 10);
    time_str[1] = '0' + (hours % 10);
    time_str[2] = ':';
    time_str[3] = '0' + (minutes / 10);
    time_str[4] = '0' + (minutes % 10);
    time_str[5] = '\0';

    fb_puts_large(FB_WIDTH - 48, taskbar_y + 8, time_str, CLOCK_COLOR, TASKBAR_BG);
}

/**
 * Draw start menu
 */
/* Start-menu app entries (in display order). Index in this array also feeds
 * the click dispatch in handle_start_menu_click. */
static const struct {
    const char *label;
    uint32_t    swatch;
} START_MENU_APPS[] = {
    { "Terminal", THEME_SUCCESS     },
    { "Files",    THEME_WARNING     },
    { "Settings", THEME_ACCENT_DIM  },
    { "About",    THEME_ACCENT      },
    { "Calc",     THEME_ACCENT      },
    { "SysMon",   THEME_SUCCESS     },
    { "Notes",    THEME_WARNING     },
    { "Tetris",   THEME_DANGER      },
};
#define START_MENU_APP_COUNT \
    (int)(sizeof(START_MENU_APPS) / sizeof(START_MENU_APPS[0]))

#define START_MENU_ITEM_H    24
#define START_MENU_SEP_H     12
#define START_MENU_PAD       8
#define START_MENU_WIDTH    150

static int start_menu_height(void)
{
    /* main apps + separator + 2 system rows + padding top/bottom */
    return START_MENU_PAD
         + START_MENU_APP_COUNT * START_MENU_ITEM_H
         + START_MENU_SEP_H
         + 2 * START_MENU_ITEM_H
         + START_MENU_PAD;
}

static void draw_start_menu(void)
{
    int32_t menu_x = 4;
    int32_t menu_height = start_menu_height();
    int32_t menu_y = FB_HEIGHT - TASKBAR_HEIGHT - menu_height - 4;
    int32_t menu_width = START_MENU_WIDTH;
    int32_t item_y;
    int     i;

    if (!desktop.start_menu_visible) {
        return;
    }

    fb_fill_rect(menu_x, menu_y, menu_width, menu_height, TASKBAR_BG);
    fb_draw_rect(menu_x, menu_y, menu_width, menu_height, TASKBAR_BORDER);

    item_y = menu_y + START_MENU_PAD;
    for (i = 0; i < START_MENU_APP_COUNT; i++) {
        fb_fill_rect(menu_x + 12, item_y + 2, 8, 8, START_MENU_APPS[i].swatch);
        fb_puts(menu_x + 26, item_y, START_MENU_APPS[i].label,
                THEME_TEXT_PRIMARY, TASKBAR_BG);
        item_y += START_MENU_ITEM_H;
    }

    /* Separator */
    fb_fill_rect(menu_x + 8, item_y, menu_width - 16, 1, TASKBAR_BORDER);
    item_y += START_MENU_SEP_H;

    fb_puts(menu_x + 26, item_y, "Text Mode", THEME_TEXT_SECONDARY, TASKBAR_BG);
    item_y += START_MENU_ITEM_H;

    fb_fill_rect(menu_x + 12, item_y + 2, 8, 8, THEME_DANGER);
    fb_puts(menu_x + 26, item_y, "Shutdown", THEME_DANGER, TASKBAR_BG);
}

/**
 * Full desktop paint
 */
void desktop_paint(void)
{
    desktop_draw_background();
    desktop_draw_icons();
    desktop_draw_taskbar();
    draw_start_menu();
}

/**
 * Find icon at position
 */
static int find_icon_at(int32_t x, int32_t y)
{
    uint32_t i;

    for (i = 0; i < desktop.icon_count; i++) {
        desktop_icon_t *icon = &desktop.icons[i];
        if (x >= icon->x && x < icon->x + ICON_WIDTH &&
            y >= icon->y && y < icon->y + ICON_HEIGHT + ICON_LABEL_HEIGHT) {
            return i;
        }
    }

    return -1;
}

/**
 * Handle start menu item click
 * Returns true if click was inside the menu and handled
 */
static bool handle_start_menu_click(int32_t x, int32_t y)
{
    int32_t menu_x = 4;
    int32_t menu_height = start_menu_height();
    int32_t menu_y = FB_HEIGHT - TASKBAR_HEIGHT - menu_height - 4;
    int32_t menu_width = START_MENU_WIDTH;
    int32_t item_y, apps_block_h;
    int     item_idx;

    if (x < menu_x || x >= menu_x + menu_width ||
        y < menu_y || y >= menu_y + menu_height) {
        return false;
    }

    item_y = y - (menu_y + START_MENU_PAD);
    if (item_y < 0) {
        return true;
    }

    apps_block_h = START_MENU_APP_COUNT * START_MENU_ITEM_H;

    if (item_y < apps_block_h) {
        item_idx = item_y / START_MENU_ITEM_H;
    } else if (item_y < apps_block_h + START_MENU_SEP_H) {
        desktop.start_menu_visible = false;
        return true;
    } else {
        item_idx = START_MENU_APP_COUNT
                 + (item_y - apps_block_h - START_MENU_SEP_H) / START_MENU_ITEM_H;
    }

    desktop.start_menu_visible = false;

    if (item_idx < START_MENU_APP_COUNT) {
        switch (item_idx) {
        case 0: gui_launch_terminal();    break;
        case 1: gui_launch_filemanager(); break;
        case 2: gui_launch_settings();    break;
        case 3: gui_launch_about();       break;
        case 4: gui_launch_calculator();  break;
        case 5: gui_launch_sysmon();      break;
        case 6: gui_launch_notes();       break;
        case 7: gui_launch_tetris();      break;
        default: break;
        }
    } else {
        switch (item_idx - START_MENU_APP_COUNT) {
        case 0: /* Text Mode — not supported from GUI */ break;
        case 1: wm_request_exit(); break;
        default: break;
        }
    }

    return true;
}

/**
 * Handle desktop click
 */
void desktop_handle_click(int32_t x, int32_t y, bool double_click)
{
    int icon_idx;
    uint64_t now = timer_get_uptime_ms();

    /* Suppress unused parameter warning - we use our own timing-based detection */
    (void)double_click;

    /* Handle start menu item clicks first */
    if (desktop.start_menu_visible) {
        if (handle_start_menu_click(x, y)) {
            return;  /* Click was inside menu and handled */
        }
        /* Click was outside menu — dismiss it */
        desktop.start_menu_visible = false;
    }

    /* Deselect current icon */
    if (desktop.selected_icon >= 0) {
        desktop.icons[desktop.selected_icon].selected = false;
    }

    /* Find clicked icon */
    icon_idx = find_icon_at(x, y);

    if (icon_idx >= 0) {
        /* Check for double-click */
        if (icon_idx == desktop.last_click_icon &&
            now - desktop.last_click_time < 500) {
            /* Double-click - launch */
            if (desktop.icons[icon_idx].on_launch) {
                desktop.icons[icon_idx].on_launch();
            }
            desktop.selected_icon = -1;
            desktop.icons[icon_idx].selected = false;
        } else {
            /* Single click - select */
            desktop.selected_icon = icon_idx;
            desktop.icons[icon_idx].selected = true;
        }

        desktop.last_click_icon = icon_idx;
        desktop.last_click_time = now;
    } else {
        desktop.selected_icon = -1;
        desktop.last_click_icon = -1;
    }
}

/**
 * Add desktop icon
 */
int desktop_add_icon(const char *name, uint32_t icon_color, void (*on_launch)(void))
{
    int col, row;

    if (desktop.icon_count >= MAX_DESKTOP_ICONS) {
        return -1;
    }

    desktop_icon_t *icon = &desktop.icons[desktop.icon_count];

    strncpy(icon->name, name, 31);
    icon->name[31] = '\0';
    icon->icon_color = icon_color;
    icon->on_launch = on_launch;
    icon->selected = false;

    /* Position in grid */
    col = desktop.icon_count % 2;  /* 2 columns */
    row = desktop.icon_count / 2;

    icon->x = ICON_MARGIN + col * ICON_SPACING;
    icon->y = ICON_MARGIN + row * (ICON_HEIGHT + ICON_LABEL_HEIGHT + 20);

    desktop.icon_count++;

    return desktop.icon_count - 1;
}

/**
 * Update taskbar
 */
void desktop_update_taskbar(void)
{
    /* Nothing special needed - taskbar is redrawn each frame */
}

/**
 * Check if click is on taskbar
 */
bool desktop_is_taskbar_click(int32_t y)
{
    return y >= (int32_t)(FB_HEIGHT - TASKBAR_HEIGHT);
}

/**
 * Handle taskbar click
 */
void desktop_handle_taskbar_click(int32_t x, int32_t y)
{
    window_t *win;
    int32_t btn_x;
    (void)y;  /* Unused */

    /* Start button */
    if (x < START_BUTTON_WIDTH + 8) {
        desktop_toggle_start_menu();
        return;
    }

    /* Close start menu on other clicks */
    desktop.start_menu_visible = false;

    /* Window buttons */
    btn_x = START_BUTTON_WIDTH + 12;
    for (win = wm_get_window_list(); win != NULL; win = win->next) {
        if (!(win->flags & WINDOW_FLAG_VISIBLE)) {
            continue;
        }

        if (x >= btn_x && x < btn_x + TASKBAR_BUTTON_WIDTH) {
            wm_focus_window(win);
            return;
        }

        btn_x += TASKBAR_BUTTON_WIDTH + 4;

        if (btn_x > FB_WIDTH - 100) {
            break;
        }
    }
}

/**
 * Get taskbar height
 */
uint32_t desktop_get_taskbar_height(void)
{
    return TASKBAR_HEIGHT;
}

/**
 * Toggle start menu
 */
void desktop_toggle_start_menu(void)
{
    desktop.start_menu_visible = !desktop.start_menu_visible;
}

/**
 * Is start menu visible
 */
bool desktop_start_menu_visible(void)
{
    return desktop.start_menu_visible;
}

void desktop_dismiss_start_menu(void)
{
    desktop.start_menu_visible = false;
}

/* ============================================================================
 * End of desktop.c
 * ============================================================================ */
