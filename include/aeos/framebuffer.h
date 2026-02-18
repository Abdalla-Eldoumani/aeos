/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/framebuffer.h
 * Description: Framebuffer graphics driver
 * ============================================================================ */

#ifndef AEOS_FRAMEBUFFER_H
#define AEOS_FRAMEBUFFER_H

#include <aeos/types.h>

/* Framebuffer configuration */
#define FB_WIDTH        640
#define FB_HEIGHT       480
#define FB_BPP          32  /* Bits per pixel (RGBA8888) */

/* Color definitions (RGBA8888 format) */
#define COLOR_BLACK     0xFF000000
#define COLOR_WHITE     0xFFFFFFFF
#define COLOR_RED       0xFFFF0000
#define COLOR_GREEN     0xFF00FF00
#define COLOR_BLUE      0xFF0000FF
#define COLOR_YELLOW    0xFFFFFF00
#define COLOR_CYAN      0xFF00FFFF
#define COLOR_MAGENTA   0xFFFF00FF
#define COLOR_GRAY      0xFF808080
#define COLOR_DARK_GRAY 0xFF404040

/* Framebuffer info structure */
typedef struct {
    uint32_t *base;         /* Base address of framebuffer */
    uint32_t width;         /* Width in pixels */
    uint32_t height;        /* Height in pixels */
    uint32_t pitch;         /* Bytes per scanline */
    uint32_t bpp;           /* Bits per pixel */
    bool initialized;       /* Initialization status */
} fb_info_t;

/**
 * Initialize framebuffer
 * @return 0 on success, -1 on error
 */
int fb_init(void);

/**
 * Get framebuffer info
 */
fb_info_t *fb_get_info(void);

/**
 * Clear screen to color
 */
void fb_clear(uint32_t color);

/**
 * Draw a pixel at (x, y)
 */
void fb_putpixel(uint32_t x, uint32_t y, uint32_t color);

/**
 * Get pixel color at (x, y)
 */
uint32_t fb_getpixel(uint32_t x, uint32_t y);

/**
 * Draw a filled rectangle (accepts signed coordinates, clips internally)
 */
void fb_fill_rect(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t color);

/**
 * Draw a rectangle outline (accepts signed coordinates, clips internally)
 */
void fb_draw_rect(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t color);

/**
 * Draw a line from (x1,y1) to (x2,y2)
 */
void fb_draw_line(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t color);

/**
 * Draw a character using 8x8 font (accepts signed coordinates)
 */
void fb_putchar(int32_t x, int32_t y, char c, uint32_t fg, uint32_t bg);

/**
 * Draw a string using 8x8 font (accepts signed coordinates)
 */
void fb_puts(int32_t x, int32_t y, const char *str, uint32_t fg, uint32_t bg);

/**
 * Scroll screen up by one line (for console mode)
 */
void fb_scroll(void);

/**
 * Console print (auto-wrapping, scrolling)
 */
void fb_console_print(const char *str, uint32_t color);

/**
 * Save framebuffer as PPM image (dumps to serial in PPM format)
 * Can be redirected to a file and viewed with any image viewer
 */
void fb_screenshot_ppm(void);

/**
 * Display ASCII art preview of framebuffer (for text mode)
 */
void fb_ascii_preview(void);

#endif /* AEOS_FRAMEBUFFER_H */

/* ============================================================================
 * End of framebuffer.h
 * ============================================================================ */
