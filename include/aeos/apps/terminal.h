/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/apps/terminal.h
 * Description: Terminal emulator application
 * ============================================================================ */

#ifndef AEOS_APPS_TERMINAL_H
#define AEOS_APPS_TERMINAL_H

#include <aeos/types.h>
#include <aeos/window.h>

/* Terminal dimensions (in characters) */
#define TERMINAL_COLS   80
#define TERMINAL_ROWS   24
#define TERMINAL_CHAR_WIDTH  8
#define TERMINAL_CHAR_HEIGHT 8

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
