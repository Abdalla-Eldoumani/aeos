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
#define PROGRESS_BAR_Y      300
#define PROGRESS_BAR_WIDTH  400
#define PROGRESS_BAR_HEIGHT 20
#define STATUS_Y            340

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

/* Simple delay using busy loop (for use before timer is ready) */
static void boot_delay(uint32_t iterations)
{
    volatile uint32_t i;
    for (i = 0; i < iterations; i++) {
        __asm__ volatile("nop");
    }
}

/**
 * Draw a filled rounded rectangle (approximated with regular rect)
 */
static void draw_rounded_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                               uint32_t color)
{
    fb_fill_rect(x + 2, y, w - 4, h, color);
    fb_fill_rect(x, y + 2, w, h - 4, color);
    fb_putpixel(x + 1, y + 1, color);
    fb_putpixel(x + w - 2, y + 1, color);
    fb_putpixel(x + 1, y + h - 2, color);
    fb_putpixel(x + w - 2, y + h - 2, color);
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
 * Draw the progress bar
 */
static void draw_progress_bar(uint32_t progress)
{
    uint32_t bar_x = (SCREEN_WIDTH - PROGRESS_BAR_WIDTH) / 2;
    uint32_t bar_y = PROGRESS_BAR_Y;
    uint32_t fill_width;

    /* Clamp progress */
    if (progress > 100) {
        progress = 100;
    }

    /* Draw background */
    draw_rounded_rect(bar_x, bar_y, PROGRESS_BAR_WIDTH, PROGRESS_BAR_HEIGHT,
                      BOOT_PROGRESS_BG);

    /* Draw fill */
    fill_width = (PROGRESS_BAR_WIDTH - 4) * progress / 100;
    if (fill_width > 0) {
        fb_fill_rect(bar_x + 2, bar_y + 2,
                     fill_width, PROGRESS_BAR_HEIGHT - 4,
                     BOOT_PROGRESS_FG);
    }

    /* Draw border */
    fb_draw_rect(bar_x, bar_y, PROGRESS_BAR_WIDTH, PROGRESS_BAR_HEIGHT,
                 BOOT_BORDER_COLOR);

    /* Draw percentage text */
    char percent_str[8];
    int i = 0;

    if (progress >= 100) {
        percent_str[i++] = '1';
        percent_str[i++] = '0';
        percent_str[i++] = '0';
    } else if (progress >= 10) {
        percent_str[i++] = '0' + (progress / 10);
        percent_str[i++] = '0' + (progress % 10);
    } else {
        percent_str[i++] = '0' + progress;
    }
    percent_str[i++] = '%';
    percent_str[i] = '\0';

    fb_puts(bar_x + PROGRESS_BAR_WIDTH + 10, bar_y + 6,
            percent_str, BOOT_TEXT_COLOR, BOOT_BG_COLOR);
}

/**
 * Draw status message
 */
static void draw_status_message(const char *message)
{
    uint32_t msg_x;
    size_t len;

    /* Clear previous message area */
    fb_fill_rect(0, STATUS_Y, SCREEN_WIDTH, 20, BOOT_BG_COLOR);

    /* Center the message */
    len = strlen(message);
    msg_x = (SCREEN_WIDTH - len * 8) / 2;

    fb_puts(msg_x, STATUS_Y, message, BOOT_TEXT_COLOR, BOOT_BG_COLOR);
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

    /* Draw status message */
    draw_status_message(current_message);

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
    if (!initialized || stage >= BOOT_STAGE_COUNT) {
        return;
    }

    /* Check for text mode request */
    check_text_mode_key();

    /* Update progress */
    current_progress = boot_stages[stage].progress;
    current_message = boot_stages[stage].message;

    /* Redraw progress bar and message */
    draw_progress_bar(current_progress);
    draw_status_message(current_message);
    refresh_display();

    /* Brief delay for visual effect */
    boot_delay(500000);
}

/**
 * Set custom boot message
 */
void bootscreen_set_progress(const char *message, uint32_t progress)
{
    if (!initialized) {
        return;
    }

    /* Check for text mode request */
    check_text_mode_key();

    current_progress = progress;
    current_message = message;

    /* Redraw */
    draw_progress_bar(current_progress);
    draw_status_message(current_message);
    refresh_display();
}

/**
 * Complete boot screen and transition
 */
bool bootscreen_complete(void)
{
    if (!initialized) {
        return true;  /* Default to GUI mode */
    }

    /* Final check for text mode */
    check_text_mode_key();

    /* Update to complete */
    current_progress = 100;
    current_message = "Boot complete!";
    draw_progress_bar(current_progress);
    draw_status_message(current_message);
    refresh_display();

    /* Short delay before transition */
    boot_delay(1000000);

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
