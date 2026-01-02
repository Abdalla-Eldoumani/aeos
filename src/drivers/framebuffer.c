/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/drivers/framebuffer.c
 * Description: Framebuffer graphics driver implementation
 * ============================================================================ */

#include <aeos/framebuffer.h>
#include <aeos/kprintf.h>
#include <aeos/string.h>
#include <aeos/heap.h>

/* Simple 8x8 font (ASCII 32-127) - each char is 8 bytes */
static const uint8_t font_8x8[96][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  /* Space */
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},  /* ! */
    {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  /* " */
    {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},  /* # */
    {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00},  /* $ */
    {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00},  /* % */
    {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00},  /* & */
    {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},  /* ' */
    {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00},  /* ( */
    {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00},  /* ) */
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},  /* * */
    {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00},  /* + */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06},  /* , */
    {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00},  /* - */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00},  /* . */
    {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00},  /* / */
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00},  /* 0 */
    {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00},  /* 1 */
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00},  /* 2 */
    {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00},  /* 3 */
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00},  /* 4 */
    {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00},  /* 5 */
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00},  /* 6 */
    {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00},  /* 7 */
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00},  /* 8 */
    {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00},  /* 9 */
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00},  /* : */
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06},  /* ; */
    {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00},  /* < */
    {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00},  /* = */
    {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00},  /* > */
    {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00},  /* ? */
    {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00},  /* @ */
    {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},  /* A */
    {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00},  /* B */
    {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00},  /* C */
    {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00},  /* D */
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00},  /* E */
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00},  /* F */
    {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00},  /* G */
    {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00},  /* H */
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},  /* I */
    {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00},  /* J */
    {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00},  /* K */
    {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00},  /* L */
    {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00},  /* M */
    {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00},  /* N */
    {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00},  /* O */
    {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00},  /* P */
    {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00},  /* Q */
    {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00},  /* R */
    {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00},  /* S */
    {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},  /* T */
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00},  /* U */
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},  /* V */
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},  /* W */
    {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00},  /* X */
    {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00},  /* Y */
    {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00},  /* Z */
    /* Add more characters as needed - for now, rest are spaces */
};

/* Framebuffer state */
static fb_info_t fb_info = {
    .base = NULL,  /* Will be allocated */
    .width = FB_WIDTH,
    .height = FB_HEIGHT,
    .pitch = FB_WIDTH * 4,
    .bpp = FB_BPP,
    .initialized = false
};

/* Console state for fb_console_print */
static struct {
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint32_t char_width;
    uint32_t char_height;
} console = {
    .cursor_x = 0,
    .cursor_y = 0,
    .char_width = 8,
    .char_height = 8
};

/**
 * Initialize framebuffer
 */
int fb_init(void)
{
    size_t fb_size;

    klog_info("Initializing framebuffer...");
    kprintf("  Resolution: %ux%u @ %u bpp\n",
            fb_info.width, fb_info.height, fb_info.bpp);

    /* Calculate framebuffer size */
    fb_size = fb_info.width * fb_info.height * (fb_info.bpp / 8);
    kprintf("  Framebuffer size: %u bytes (%u KB)\n",
            (uint32_t)fb_size, (uint32_t)(fb_size / 1024));

    /* Allocate framebuffer from heap */
    fb_info.base = (uint32_t *)kmalloc(fb_size);
    if (fb_info.base == NULL) {
        klog_error("Failed to allocate framebuffer memory");
        return -1;
    }

    kprintf("  Framebuffer allocated at: %p\n", fb_info.base);

    /* Mark as initialized */
    fb_info.initialized = true;

    /* Clear screen to black */
    fb_clear(COLOR_BLACK);

    klog_info("Framebuffer initialized (virtual mode - in memory)");
    klog_info("NOTE: Graphics rendered to memory, not displayed in text QEMU");
    klog_info("      A real GPU driver would send this to display hardware");

    return 0;
}

/**
 * Get framebuffer info
 */
fb_info_t *fb_get_info(void)
{
    return &fb_info;
}

/**
 * Clear screen to color
 */
void fb_clear(uint32_t color)
{
    uint32_t i;
    uint32_t pixels = fb_info.width * fb_info.height;

    if (!fb_info.initialized) {
        return;
    }

    for (i = 0; i < pixels; i++) {
        fb_info.base[i] = color;
    }
}

/**
 * Draw a pixel at (x, y)
 */
void fb_putpixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (!fb_info.initialized || x >= fb_info.width || y >= fb_info.height) {
        return;
    }

    fb_info.base[y * fb_info.width + x] = color;
}

/**
 * Get pixel color at (x, y)
 */
uint32_t fb_getpixel(uint32_t x, uint32_t y)
{
    if (!fb_info.initialized || x >= fb_info.width || y >= fb_info.height) {
        return 0;
    }

    return fb_info.base[y * fb_info.width + x];
}

/**
 * Draw a filled rectangle
 */
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
{
    uint32_t i, j;

    if (!fb_info.initialized) {
        return;
    }

    for (j = 0; j < height; j++) {
        for (i = 0; i < width; i++) {
            if (x + i < fb_info.width && y + j < fb_info.height) {
                fb_putpixel(x + i, y + j, color);
            }
        }
    }
}

/**
 * Draw a rectangle outline
 */
void fb_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
{
    uint32_t i;

    if (!fb_info.initialized) {
        return;
    }

    /* Top and bottom edges */
    for (i = 0; i < width; i++) {
        fb_putpixel(x + i, y, color);
        fb_putpixel(x + i, y + height - 1, color);
    }

    /* Left and right edges */
    for (i = 0; i < height; i++) {
        fb_putpixel(x, y + i, color);
        fb_putpixel(x + width - 1, y + i, color);
    }
}

/**
 * Draw a line from (x1,y1) to (x2,y2) using Bresenham's algorithm
 */
void fb_draw_line(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t color)
{
    int dx = x2 > x1 ? x2 - x1 : x1 - x2;
    int dy = y2 > y1 ? y2 - y1 : y1 - y2;
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;
    int e2;

    while (1) {
        fb_putpixel(x1, y1, color);

        if (x1 == x2 && y1 == y2) {
            break;
        }

        e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

/**
 * Draw a character using 8x8 font
 */
void fb_putchar(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg)
{
    uint32_t i, j;
    uint8_t row;
    const uint8_t *glyph;

    if (!fb_info.initialized) {
        return;
    }

    /* Get glyph for character */
    if (c < 32 || c > 127) {
        c = ' ';
    }
    glyph = font_8x8[c - 32];

    /* Draw character */
    for (j = 0; j < 8; j++) {
        row = glyph[j];
        for (i = 0; i < 8; i++) {
            if (row & (1 << (7 - i))) {
                fb_putpixel(x + i, y + j, fg);
            } else {
                fb_putpixel(x + i, y + j, bg);
            }
        }
    }
}

/**
 * Draw a string using 8x8 font
 */
void fb_puts(uint32_t x, uint32_t y, const char *str, uint32_t fg, uint32_t bg)
{
    uint32_t offset_x = 0;

    if (!fb_info.initialized || str == NULL) {
        return;
    }

    while (*str) {
        fb_putchar(x + offset_x, y, *str, fg, bg);
        offset_x += 8;
        str++;
    }
}

/**
 * Scroll screen up by one line
 */
void fb_scroll(void)
{
    uint32_t line_height = console.char_height;
    uint32_t bytes_per_line = fb_info.width * 4 * line_height;
    uint32_t total_lines = fb_info.height - line_height;

    if (!fb_info.initialized) {
        return;
    }

    /* Move all lines up by one line height */
    memmove(fb_info.base,
            (uint8_t *)fb_info.base + bytes_per_line,
            total_lines * fb_info.width * 4);

    /* Clear bottom line */
    fb_fill_rect(0, fb_info.height - line_height,
                 fb_info.width, line_height, COLOR_BLACK);
}

/**
 * Console print (auto-wrapping, scrolling)
 */
void fb_console_print(const char *str, uint32_t color)
{
    if (!fb_info.initialized || str == NULL) {
        return;
    }

    while (*str) {
        if (*str == '\n') {
            console.cursor_x = 0;
            console.cursor_y += console.char_height;
        } else if (*str == '\r') {
            console.cursor_x = 0;
        } else {
            /* Draw character */
            fb_putchar(console.cursor_x, console.cursor_y, *str, color, COLOR_BLACK);
            console.cursor_x += console.char_width;

            /* Wrap to next line if needed */
            if (console.cursor_x >= fb_info.width) {
                console.cursor_x = 0;
                console.cursor_y += console.char_height;
            }
        }

        /* Scroll if needed */
        if (console.cursor_y >= fb_info.height) {
            fb_scroll();
            console.cursor_y = fb_info.height - console.char_height;
        }

        str++;
    }
}

/**
 * Save framebuffer as PPM image
 * PPM is a simple image format that can be viewed with any image viewer
 */
void fb_screenshot_ppm(void)
{
    uint32_t x, y;
    uint32_t pixel;
    uint8_t r, g, b;

    if (!fb_info.initialized) {
        return;
    }

    /* PPM header */
    kprintf("P3\n");
    kprintf("%u %u\n", fb_info.width, fb_info.height);
    kprintf("255\n");

    /* Pixel data */
    for (y = 0; y < fb_info.height; y++) {
        for (x = 0; x < fb_info.width; x++) {
            pixel = fb_info.base[y * fb_info.width + x];

            /* Extract RGB (RGBA8888 format) */
            r = (pixel >> 16) & 0xFF;
            g = (pixel >> 8) & 0xFF;
            b = pixel & 0xFF;

            kprintf("%u %u %u ", r, g, b);
        }
        kprintf("\n");
    }
}

/**
 * Display ASCII art preview (downsampled)
 */
void fb_ascii_preview(void)
{
    uint32_t x, y;
    uint32_t pixel;
    uint8_t gray;
    char c;
    const char *chars = " .:;+=xX$@";  /* Brightness gradient */
    uint32_t sample_x, sample_y;

    if (!fb_info.initialized) {
        return;
    }

    kprintf("\n=== FRAMEBUFFER PREVIEW (80x24 ASCII) ===\n\n");

    /* Sample every 8x20 pixels to fit in 80x24 terminal */
    for (y = 0; y < 24; y++) {
        for (x = 0; x < 80; x++) {
            sample_x = (x * fb_info.width) / 80;
            sample_y = (y * fb_info.height) / 24;

            pixel = fb_info.base[sample_y * fb_info.width + sample_x];

            /* Convert to grayscale */
            gray = ((pixel >> 16) & 0xFF) * 30 / 100 +  /* R * 0.3 */
                   ((pixel >> 8) & 0xFF) * 59 / 100 +   /* G * 0.59 */
                   (pixel & 0xFF) * 11 / 100;           /* B * 0.11 */

            /* Map to ASCII character */
            c = chars[(gray * 9) / 255];
            kprintf("%c", c);
        }
        kprintf("\n");
    }

    kprintf("\n=== END PREVIEW ===\n\n");
}

/* ============================================================================
 * End of framebuffer.c
 * ============================================================================ */
