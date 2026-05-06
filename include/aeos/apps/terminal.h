/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/apps/terminal.h
 * Description: Terminal emulator application
 * ============================================================================ */

#ifndef AEOS_APPS_TERMINAL_H
#define AEOS_APPS_TERMINAL_H

#include <aeos/types.h>
#include <aeos/window.h>

/* Cell metrics. Width stays 8 (the only width our font ships at). Height
 * doubles so the terminal text matches the rest of the OS now that 8x16 is
 * the standard body font. Internal padding sits between the cell grid and
 * the window's client edges. */
#define TERMINAL_CHAR_WIDTH      8
#define TERMINAL_CHAR_HEIGHT    16
#define TERMINAL_PAD_X           4
#define TERMINAL_PAD_Y           4

/* Window dimensions are fixed first; the cell grid is then computed from the
 * available client area. The window must fit within the 640x480 framebuffer
 * after the 32 px taskbar. 634x385 yields 78 cols x 22 rows. */
#define TERMINAL_WIN_WIDTH      634
#define TERMINAL_WIN_HEIGHT     385

#define TERMINAL_CLIENT_WIDTH   (TERMINAL_WIN_WIDTH  - 2 * WINDOW_BORDER_WIDTH)
#define TERMINAL_CLIENT_HEIGHT  (TERMINAL_WIN_HEIGHT - WINDOW_TITLE_HEIGHT - WINDOW_BORDER_WIDTH)
#define TERMINAL_COLS           ((TERMINAL_CLIENT_WIDTH  - 2 * TERMINAL_PAD_X) / TERMINAL_CHAR_WIDTH)
#define TERMINAL_ROWS           ((TERMINAL_CLIENT_HEIGHT - 2 * TERMINAL_PAD_Y) / TERMINAL_CHAR_HEIGHT)

/* Terminal colors (ANSI-like) */
#define TERM_COLOR_BLACK    0
#define TERM_COLOR_RED      1
#define TERM_COLOR_GREEN    2
#define TERM_COLOR_YELLOW   3
#define TERM_COLOR_BLUE     4
#define TERM_COLOR_MAGENTA  5
#define TERM_COLOR_CYAN     6
#define TERM_COLOR_WHITE    7

/* Terminal cell structure */
typedef struct {
    char ch;
    uint8_t fg;
    uint8_t bg;
    uint8_t attr;
} terminal_cell_t;

/* ANSI escape parser state */
typedef enum {
    ANSI_NORMAL = 0,
    ANSI_ESC    = 1,   /* saw ESC, expect '[' */
    ANSI_CSI    = 2    /* inside CSI, collecting params and final byte */
} ansi_state_t;

/* Terminal state */
typedef struct {
    window_t *window;
    terminal_cell_t cells[TERMINAL_ROWS][TERMINAL_COLS];
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint8_t current_fg;
    uint8_t current_bg;
    bool cursor_visible;
    bool cursor_blink_state;
    uint64_t last_blink;

    /* Input buffer for shell */
    char input_buffer[256];
    uint32_t input_pos;
    bool input_ready;

    /* Scroll buffer */
    uint32_t scroll_offset;

    /* ANSI escape sequence parser */
    uint8_t  ansi_state;        /* one of ansi_state_t */
    bool     ansi_private;      /* CSI '?' modifier seen */
    bool     ansi_seen_param;   /* at least one digit consumed in current CSI */
    uint8_t  ansi_param_count;  /* index of parameter currently being filled */
    uint16_t ansi_params[8];
} terminal_t;

/**
 * Create and show terminal window
 * @return Terminal structure or NULL on error
 */
terminal_t *terminal_create(void);

/**
 * Destroy terminal
 */
void terminal_destroy(terminal_t *term);

/**
 * Write a character to terminal
 */
void terminal_putchar(terminal_t *term, char c);

/**
 * Write a string to terminal
 */
void terminal_puts(terminal_t *term, const char *str);

/**
 * Write formatted string to terminal
 */
void terminal_printf(terminal_t *term, const char *fmt, ...);

/**
 * Clear terminal
 */
void terminal_clear(terminal_t *term);

/**
 * Set terminal colors
 */
void terminal_set_color(terminal_t *term, uint8_t fg, uint8_t bg);

/**
 * Handle key input for terminal
 */
void terminal_handle_key(terminal_t *term, key_event_t *key);

/**
 * Execute shell command in terminal
 */
void terminal_execute_command(terminal_t *term, const char *cmd);

/**
 * Show shell prompt
 */
void terminal_show_prompt(terminal_t *term);

/**
 * Get global terminal instance (for output redirection)
 */
terminal_t *terminal_get_active(void);

#endif /* AEOS_APPS_TERMINAL_H */
