/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/apps/terminal.c
 * Description: Terminal emulator application
 * ============================================================================ */

#include <aeos/apps/terminal.h>
#include <aeos/window.h>
#include <aeos/wm.h>
#include <aeos/framebuffer.h>
#include <aeos/shell.h>
#include <aeos/heap.h>
#include <aeos/string.h>
#include <aeos/kprintf.h>
#include <aeos/timer.h>

/* Terminal colors (RGB values) */
static const uint32_t term_colors[] = {
    0xFF000000,  /* Black */
    0xFFCC0000,  /* Red */
    0xFF00CC00,  /* Green */
    0xFFCCCC00,  /* Yellow */
    0xFF0066CC,  /* Blue */
    0xFFCC00CC,  /* Magenta */
    0xFF00CCCC,  /* Cyan */
    0xFFCCCCCC,  /* White */
    /* Bright variants */
    0xFF666666,  /* Bright Black */
    0xFFFF0000,  /* Bright Red */
    0xFF00FF00,  /* Bright Green */
    0xFFFFFF00,  /* Bright Yellow */
    0xFF0099FF,  /* Bright Blue */
    0xFFFF00FF,  /* Bright Magenta */
    0xFF00FFFF,  /* Bright Cyan */
    0xFFFFFFFF   /* Bright White */
};

/* Active terminal for output redirection */
static terminal_t *active_terminal = NULL;

/* Forward declarations */
static void terminal_paint(window_t *win);
static void terminal_key(window_t *win, key_event_t *key);
static void terminal_close(window_t *win);

/**
 * Scroll terminal up by one line
 */
static void terminal_scroll(terminal_t *term)
{
    uint32_t row, col;

    /* Move all lines up */
    for (row = 0; row < TERMINAL_ROWS - 1; row++) {
        for (col = 0; col < TERMINAL_COLS; col++) {
            term->cells[row][col] = term->cells[row + 1][col];
        }
    }

    /* Clear last line */
    for (col = 0; col < TERMINAL_COLS; col++) {
        term->cells[TERMINAL_ROWS - 1][col].ch = ' ';
        term->cells[TERMINAL_ROWS - 1][col].fg = term->current_fg;
        term->cells[TERMINAL_ROWS - 1][col].bg = term->current_bg;
    }
}

/**
 * Create terminal
 */
terminal_t *terminal_create(void)
{
    terminal_t *term;
    uint32_t win_width, win_height;
    uint32_t row, col;

    term = (terminal_t *)kmalloc(sizeof(terminal_t));
    if (!term) {
        klog_error("Failed to allocate terminal");
        return NULL;
    }

    memset(term, 0, sizeof(terminal_t));

    /* Calculate window size */
    win_width = TERMINAL_COLS * TERMINAL_CHAR_WIDTH + 2 * WINDOW_BORDER_WIDTH + 8;
    win_height = TERMINAL_ROWS * TERMINAL_CHAR_HEIGHT + WINDOW_TITLE_HEIGHT +
                 WINDOW_BORDER_WIDTH + 8;

    /* Create window */
    term->window = window_create("Terminal", 100, 50, win_width, win_height,
                                  WINDOW_FLAG_VISIBLE);
    if (!term->window) {
        kfree(term);
        return NULL;
    }

    /* Set callbacks */
    term->window->on_paint = terminal_paint;
    term->window->on_key = terminal_key;
    term->window->on_close = terminal_close;
    term->window->user_data = term;

    /* Initialize state */
    term->cursor_x = 0;
    term->cursor_y = 0;
    term->current_fg = TERM_COLOR_WHITE;
    term->current_bg = TERM_COLOR_BLACK;
    term->cursor_visible = true;
    term->cursor_blink_state = true;
    term->last_blink = timer_get_uptime_ms();
    term->input_pos = 0;
    term->input_ready = false;

    /* Clear cells */
    for (row = 0; row < TERMINAL_ROWS; row++) {
        for (col = 0; col < TERMINAL_COLS; col++) {
            term->cells[row][col].ch = ' ';
            term->cells[row][col].fg = TERM_COLOR_WHITE;
            term->cells[row][col].bg = TERM_COLOR_BLACK;
        }
    }

    /* Register with window manager */
    wm_register_window(term->window);

    /* Set as active terminal */
    active_terminal = term;

    /* Show welcome message and prompt */
    terminal_puts(term, "AEOS Terminal v1.0\n");
    terminal_puts(term, "Type 'help' for available commands.\n\n");
    terminal_show_prompt(term);

    klog_debug("Terminal created: %ux%u chars", TERMINAL_COLS, TERMINAL_ROWS);

    return term;
}

/**
 * Destroy terminal
 */
void terminal_destroy(terminal_t *term)
{
    if (!term) {
        return;
    }

    if (active_terminal == term) {
        active_terminal = NULL;
    }

    /* Window destruction handled by close callback */
    kfree(term);
}

/**
 * Draw terminal content
 */
static void terminal_paint(window_t *win)
{
    terminal_t *term = (terminal_t *)win->user_data;
    uint32_t row, col;
    uint32_t fg, bg;
    int32_t x, y;

    if (!term) {
        return;
    }

    /* Clear background */
    window_clear(win, term_colors[TERM_COLOR_BLACK]);

    /* Draw cells */
    for (row = 0; row < TERMINAL_ROWS; row++) {
        y = row * TERMINAL_CHAR_HEIGHT + 2;
        for (col = 0; col < TERMINAL_COLS; col++) {
            x = col * TERMINAL_CHAR_WIDTH + 4;

            fg = term_colors[term->cells[row][col].fg & 0x0F];
            bg = term_colors[term->cells[row][col].bg & 0x0F];

            window_putchar(win, x, y, term->cells[row][col].ch, fg, bg);
        }
    }

    /* Draw cursor */
    if (term->cursor_visible && term->cursor_blink_state) {
        x = term->cursor_x * TERMINAL_CHAR_WIDTH + 4;
        y = term->cursor_y * TERMINAL_CHAR_HEIGHT + 2;
        window_fill_rect(win, x, y, TERMINAL_CHAR_WIDTH, TERMINAL_CHAR_HEIGHT,
                         term_colors[TERM_COLOR_WHITE]);

        /* Draw character under cursor in inverse */
        if (term->cells[term->cursor_y][term->cursor_x].ch != ' ') {
            window_putchar(win, x, y,
                           term->cells[term->cursor_y][term->cursor_x].ch,
                           term_colors[TERM_COLOR_BLACK],
                           term_colors[TERM_COLOR_WHITE]);
        }
    }

    /* Update cursor blink */
    uint64_t now = timer_get_uptime_ms();
    if (now - term->last_blink > 500) {
        term->cursor_blink_state = !term->cursor_blink_state;
        term->last_blink = now;
        window_invalidate(win);
    }
}

/**
 * Handle key input
 */
static void terminal_key(window_t *win, key_event_t *key)
{
    terminal_t *term = (terminal_t *)win->user_data;

    if (!term) {
        return;
    }

    terminal_handle_key(term, key);
    window_invalidate(win);
}

/**
 * Handle close
 */
static void terminal_close(window_t *win)
{
    terminal_t *term = (terminal_t *)win->user_data;

    wm_unregister_window(win);
    window_destroy(win);

    if (term) {
        if (active_terminal == term) {
            active_terminal = NULL;
        }
        kfree(term);
    }
}

/**
 * Write character to terminal
 */
void terminal_putchar(terminal_t *term, char c)
{
    if (!term) {
        return;
    }

    if (c == '\n') {
        /* Newline */
        term->cursor_x = 0;
        term->cursor_y++;
    } else if (c == '\r') {
        /* Carriage return */
        term->cursor_x = 0;
    } else if (c == '\b') {
        /* Backspace */
        if (term->cursor_x > 0) {
            term->cursor_x--;
        }
    } else if (c == '\t') {
        /* Tab */
        term->cursor_x = (term->cursor_x + 8) & ~7;
    } else if (c >= 32 && c < 127) {
        /* Printable character */
        term->cells[term->cursor_y][term->cursor_x].ch = c;
        term->cells[term->cursor_y][term->cursor_x].fg = term->current_fg;
        term->cells[term->cursor_y][term->cursor_x].bg = term->current_bg;
        term->cursor_x++;
    }

    /* Handle line wrap */
    if (term->cursor_x >= TERMINAL_COLS) {
        term->cursor_x = 0;
        term->cursor_y++;
    }

    /* Handle scroll */
    while (term->cursor_y >= TERMINAL_ROWS) {
        terminal_scroll(term);
        term->cursor_y--;
    }

    window_invalidate(term->window);
}

/**
 * Write string to terminal
 */
void terminal_puts(terminal_t *term, const char *str)
{
    if (!term || !str) {
        return;
    }

    while (*str) {
        terminal_putchar(term, *str++);
    }
}

/**
 * Clear terminal
 */
void terminal_clear(terminal_t *term)
{
    uint32_t row, col;

    if (!term) {
        return;
    }

    for (row = 0; row < TERMINAL_ROWS; row++) {
        for (col = 0; col < TERMINAL_COLS; col++) {
            term->cells[row][col].ch = ' ';
            term->cells[row][col].fg = term->current_fg;
            term->cells[row][col].bg = term->current_bg;
        }
    }

    term->cursor_x = 0;
    term->cursor_y = 0;

    window_invalidate(term->window);
}

/**
 * Set terminal colors
 */
void terminal_set_color(terminal_t *term, uint8_t fg, uint8_t bg)
{
    if (!term) {
        return;
    }

    term->current_fg = fg & 0x0F;
    term->current_bg = bg & 0x0F;
}

/**
 * Show shell prompt
 */
void terminal_show_prompt(terminal_t *term)
{
    if (!term) {
        return;
    }

    terminal_set_color(term, TERM_COLOR_GREEN, TERM_COLOR_BLACK);
    terminal_puts(term, "aeos");
    terminal_set_color(term, TERM_COLOR_WHITE, TERM_COLOR_BLACK);
    terminal_puts(term, ":");
    terminal_set_color(term, TERM_COLOR_BLUE, TERM_COLOR_BLACK);
    terminal_puts(term, "~");
    terminal_set_color(term, TERM_COLOR_WHITE, TERM_COLOR_BLACK);
    terminal_puts(term, "$ ");
}

/**
 * Execute shell command
 */
void terminal_execute_command(terminal_t *term, const char *cmd)
{
    char line[SHELL_MAX_LINE];
    char *argv[SHELL_MAX_ARGS];
    int argc;

    if (!term || !cmd || cmd[0] == '\0') {
        return;
    }

    /* Copy command to mutable buffer */
    strncpy(line, cmd, SHELL_MAX_LINE - 1);
    line[SHELL_MAX_LINE - 1] = '\0';

    /* Handle clear specially */
    if (strcmp(line, "clear") == 0) {
        terminal_clear(term);
        return;
    }

    /* Parse command */
    if (shell_parse(line, &argc, argv) == 0 && argc > 0) {
        /* Set active terminal for output capture */
        active_terminal = term;

        /* Execute */
        shell_execute(argc, argv);
    }
}

/**
 * Handle key input
 */
void terminal_handle_key(terminal_t *term, key_event_t *key)
{
    if (!term) {
        return;
    }

    /* Handle printable characters */
    if (key->ascii >= 32 && key->ascii < 127) {
        if (term->input_pos < sizeof(term->input_buffer) - 1) {
            term->input_buffer[term->input_pos++] = key->ascii;
            terminal_putchar(term, key->ascii);
        }
        return;
    }

    /* Handle special keys */
    switch (key->keycode) {
        case KEY_ENTER:
            terminal_putchar(term, '\n');
            term->input_buffer[term->input_pos] = '\0';

            /* Execute command */
            if (term->input_pos > 0) {
                terminal_execute_command(term, term->input_buffer);
            }

            /* Reset input and show prompt */
            term->input_pos = 0;
            term->input_buffer[0] = '\0';
            terminal_show_prompt(term);
            break;

        case KEY_BACKSPACE:
            if (term->input_pos > 0) {
                term->input_pos--;
                term->input_buffer[term->input_pos] = '\0';

                /* Erase character visually */
                if (term->cursor_x > 0) {
                    term->cursor_x--;
                    term->cells[term->cursor_y][term->cursor_x].ch = ' ';
                }
                window_invalidate(term->window);
            }
            break;

        case KEY_ESCAPE:
            /* Clear input line */
            while (term->input_pos > 0) {
                term->input_pos--;
                if (term->cursor_x > 0) {
                    term->cursor_x--;
                    term->cells[term->cursor_y][term->cursor_x].ch = ' ';
                }
            }
            term->input_buffer[0] = '\0';
            window_invalidate(term->window);
            break;

        default:
            break;
    }
}

/**
 * Get active terminal
 */
terminal_t *terminal_get_active(void)
{
    return active_terminal;
}

/* ============================================================================
 * End of terminal.c
 * ============================================================================ */
