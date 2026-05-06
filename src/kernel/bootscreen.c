/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/kernel/bootscreen.c
 * Description: Animated boot screen implementation
 * ============================================================================ */

#include <aeos/bootscreen.h>
#include <aeos/framebuffer.h>
#include <aeos/virtio_gpu.h>
#include <aeos/uart.h>
#include <aeos/kprintf.h>
#include <aeos/string.h>
#include <aeos/theme.h>
#include <aeos/anim.h>
#include <aeos/timer.h>

/* Color definitions for boot screen */
#define BOOT_BG_COLOR       THEME_BG_DEEP
#define BOOT_LOGO_COLOR     THEME_ACCENT
#define BOOT_LOGO_SHADOW    THEME_ACCENT_DIM
#define BOOT_TEXT_COLOR     THEME_TEXT_PRIMARY
#define BOOT_PROGRESS_BG    THEME_SURFACE_2
#define BOOT_PROGRESS_FG    THEME_ACCENT
#define BOOT_BORDER_COLOR   THEME_BORDER_SUBTLE

/* Screen layout constants. Vertical positions follow DESIGN_SYSTEM.md
 * (logo 25% from top, progress bar 55% from top, footer 8 px from bottom). */
#define SCREEN_WIDTH        640
#define SCREEN_HEIGHT       480
#define LOGO_Y              120
#define LOGO_WORDMARK_W     35   /* 4 glyphs of 8 px + 3 px tracking */
#define LOGO_WORDMARK_H     16
#define LOGO_SUBTITLE_GAP   8
#define PROGRESS_BAR_Y      264   /* 55% of 480 */
#define PROGRESS_BAR_WIDTH  384   /* 60% of 640 */
#define PROGRESS_BAR_HEIGHT 4
#define STATUS_Y            276   /* 8 px below progress bar */
#define STAGE_FADE_MS       240u  /* duration of stage-message cross-fade */

/* Boot stage information */
static const boot_stage_info_t boot_stages[] = {
    { "Initializing memory...",      10 },
    { "Setting up interrupts...",    20 },
    { "Starting timer...",           30 },
    { "Loading file system...",      50 },
    { "Initializing processes...",   70 },
    { "Starting input devices...",   85 },
    { "Loading desktop...",          100 },
    { "Boot complete!",              100 }
};

/* State */
static bool initialized = false;
static bool text_mode_requested = false;
static uint32_t current_progress = 0;
static const char *current_message = "Booting...";

/* Forward decls so fade_status_message can use them. */
static void refresh_display(void);
static uint32_t blend_color(uint32_t a, uint32_t b, int32_t t_q8);

/* Simple delay using busy loop (for use before timer is ready) */
static void boot_delay(uint32_t iterations)
{
    volatile uint32_t i;
    for (i = 0; i < iterations; i++) {
        __asm__ volatile("nop");
    }
}

/**
 * Draw the AEOS wordmark (8x16 letters, tracked-out by 1 px) at (x, y),
 * with the "Educational Operating System" subtitle in 8x8 centered below.
 */
void bootscreen_draw_logo(uint32_t x, uint32_t y)
{
    static const char wordmark[4] = { 'A', 'E', 'O', 'S' };
    const char *subtitle = "Educational Operating System";
    size_t sub_len;
    int32_t sub_x, sub_y;
    int i;

    for (i = 0; i < 4; i++) {
        fb_putchar_large((int32_t)x + i * 9, (int32_t)y, wordmark[i],
                         BOOT_LOGO_COLOR, BOOT_BG_COLOR);
    }

    sub_len = strlen(subtitle);
    sub_x = (int32_t)((SCREEN_WIDTH - (uint32_t)(sub_len * 8)) / 2);
    sub_y = (int32_t)y + LOGO_WORDMARK_H + LOGO_SUBTITLE_GAP;
    fb_puts(sub_x, sub_y, subtitle, THEME_TEXT_SECONDARY, BOOT_BG_COLOR);
}

/**
 * Draw a slim 4 px progress bar centered horizontally.
 */
static void draw_progress_bar(uint32_t progress)
{
    uint32_t bar_x = (SCREEN_WIDTH - PROGRESS_BAR_WIDTH) / 2;
    uint32_t fill_width;

    if (progress > 100) {
        progress = 100;
    }

    fb_fill_rect((int32_t)bar_x, (int32_t)PROGRESS_BAR_Y,
                 (int32_t)PROGRESS_BAR_WIDTH, (int32_t)PROGRESS_BAR_HEIGHT,
                 BOOT_PROGRESS_BG);

    fill_width = PROGRESS_BAR_WIDTH * progress / 100;
    if (fill_width > 0) {
        fb_fill_rect((int32_t)bar_x, (int32_t)PROGRESS_BAR_Y,
                     (int32_t)fill_width, (int32_t)PROGRESS_BAR_HEIGHT,
                     BOOT_PROGRESS_FG);
    }
}

/**
 * Linear blend between two RGBA colors. t_q8 in 0..256 (Q0.8): 0 -> a, 256 -> b.
 */
static uint32_t blend_color(uint32_t a, uint32_t b, int32_t t_q8)
{
    uint32_t inv;
    uint32_t ar, ag, ab, br, bg, bb, r, g, bl;

    if (t_q8 <= 0) return a;
    if (t_q8 >= ANIM_Q8_ONE) return b;

    inv = (uint32_t)(ANIM_Q8_ONE - t_q8);
    ar = (a >> 16) & 0xFFu; ag = (a >> 8) & 0xFFu; ab = a & 0xFFu;
    br = (b >> 16) & 0xFFu; bg = (b >> 8) & 0xFFu; bb = b & 0xFFu;
    r  = (ar * inv + br * (uint32_t)t_q8) >> 8;
    g  = (ag * inv + bg * (uint32_t)t_q8) >> 8;
    bl = (ab * inv + bb * (uint32_t)t_q8) >> 8;
    return 0xFF000000u | (r << 16) | (g << 8) | bl;
}

/**
 * Draw the status message in the given foreground color, centered on STATUS_Y.
 * Caller is responsible for clearing the row first if required.
 */
static void draw_status_message(const char *message, uint32_t fg)
{
    int32_t msg_x;
    size_t len;

    if (message == NULL) {
        return;
    }
    len = strlen(message);
    msg_x = (int32_t)((SCREEN_WIDTH - (uint32_t)(len * 8)) / 2);
    fb_puts(msg_x, (int32_t)STATUS_Y, message, fg, BOOT_BG_COLOR);
}

/**
 * Cross-fade the status row between two messages over STAGE_FADE_MS using
 * cubic ease-out. Pass from=NULL to fade only the new message in.
 */
static void fade_status_message(const char *from, const char *to)
{
    uint64_t start, now;
    int32_t t_q8, eased;
    uint32_t fg_from, fg_to;

    if (from == to) {
        fb_fill_rect(0, (int32_t)STATUS_Y, (int32_t)SCREEN_WIDTH, 8, BOOT_BG_COLOR);
        draw_status_message(to, THEME_TEXT_SECONDARY);
        refresh_display();
        return;
    }

    start = timer_get_uptime_ms();
    do {
        now = timer_get_uptime_ms();
        t_q8 = anim_progress_q8(now, start, STAGE_FADE_MS);
        eased = ease_out_cubic_q8(t_q8);

        fb_fill_rect(0, (int32_t)STATUS_Y, (int32_t)SCREEN_WIDTH, 8, BOOT_BG_COLOR);

        if (from != NULL) {
            fg_from = blend_color(THEME_TEXT_SECONDARY, BOOT_BG_COLOR, eased);
            draw_status_message(from, fg_from);
        }
        fg_to = blend_color(BOOT_BG_COLOR, THEME_TEXT_SECONDARY, eased);
        draw_status_message(to, fg_to);

        refresh_display();
    } while (t_q8 < ANIM_Q8_ONE);
}

/**
 * Draw copyright/version info at bottom
 */
static void draw_footer(void)
{
    fb_puts(10, SCREEN_HEIGHT - 20, "AEOS v1.0 - Abdalla's Educational OS",
            THEME_TEXT_MUTED, BOOT_BG_COLOR);
    fb_puts(SCREEN_WIDTH - 180, SCREEN_HEIGHT - 20, "ARMv8-A AArch64",
            THEME_TEXT_MUTED, BOOT_BG_COLOR);
}

/**
 * Draw the complete boot screen
 */
static void draw_boot_screen(void)
{
    /* Clear screen with background color */
    fb_clear(BOOT_BG_COLOR);

    /* Draw logo centered */
    bootscreen_draw_logo((SCREEN_WIDTH - LOGO_WORDMARK_W) / 2, LOGO_Y);

    /* Draw progress bar */
    draw_progress_bar(current_progress);

    /* Draw status message (fb_clear above already wiped the row). */
    draw_status_message(current_message, THEME_TEXT_SECONDARY);

    /* Draw footer */
    draw_footer();

    /* Draw hint for text mode */
    fb_puts((SCREEN_WIDTH - 240) / 2, SCREEN_HEIGHT - 50,
            "Press 'T' for text mode", THEME_TEXT_MUTED, BOOT_BG_COLOR);
}

/**
 * Update display (refresh GPU)
 */
static void refresh_display(void)
{
    virtio_gpu_update_display();
}

/**
 * Check UART for 'T' key press
 */
static void check_text_mode_key(void)
{
    if (uart_data_available()) {
        char c = uart_getc();
        if (c == 'T' || c == 't') {
            text_mode_requested = true;
            klog_info("Text mode requested by user");
        }
    }
}

/**
 * Initialize and display boot screen
 */
void bootscreen_init(void)
{
    klog_info("Initializing boot screen...");

    initialized = true;
    current_progress = 0;
    current_message = "Starting AEOS...";
    text_mode_requested = false;

    /* Draw initial boot screen */
    draw_boot_screen();
    refresh_display();

    klog_info("Boot screen initialized");
}

/**
 * Update boot progress to specified stage
 */
void bootscreen_update(boot_stage_t stage)
{
    const char *old_message;

    if (!initialized || stage >= BOOT_STAGE_COUNT) {
        return;
    }

    check_text_mode_key();

    old_message = current_message;
    current_progress = boot_stages[stage].progress;
    current_message = boot_stages[stage].message;

    draw_progress_bar(current_progress);
    fade_status_message(old_message, current_message);
}

/**
 * Set custom boot message
 */
void bootscreen_set_progress(const char *message, uint32_t progress)
{
    const char *old_message;

    if (!initialized) {
        return;
    }

    check_text_mode_key();

    old_message = current_message;
    current_progress = progress;
    current_message = message;

    draw_progress_bar(current_progress);
    fade_status_message(old_message, current_message);
}

/**
 * Complete boot screen and transition
 */
bool bootscreen_complete(void)
{
    const char *old_message;

    if (!initialized) {
        return true;  /* Default to GUI mode */
    }

    check_text_mode_key();

    old_message = current_message;
    current_progress = 100;
    current_message = "Boot complete!";

    draw_progress_bar(current_progress);
    fade_status_message(old_message, current_message);

    /* Brief pause so the user can register "Boot complete!" before transition. */
    boot_delay(800000);

    return !text_mode_requested;
}

/**
 * Check if text mode was requested
 */
bool bootscreen_text_mode_requested(void)
{
    return text_mode_requested;
}

/* ============================================================================
 * End of bootscreen.c
 * ============================================================================ */
