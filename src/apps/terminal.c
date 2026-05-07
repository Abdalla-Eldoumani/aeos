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

/* Terminal colors. These are the standard ANSI 16-color SGR palette and stay
 * literal on purpose — they're the spec, not design tokens. Theme colors live
 * in include/aeos/theme.h. */
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

/**
 * kprintf output hook — sends characters to the active terminal
 */
static void terminal_kprintf_hook(char c)
{
    if (active_terminal) {
        terminal_putchar(active_terminal, c);
    }
}

/* Forward declarations */
static void terminal_paint(window_t *win);
static void terminal_key(window_t *win, key_event_t *key);
static void terminal_close(window_t *win);
static void ansi_feed(terminal_t *term, char c);
static void ansi_dispatch(terminal_t *term, char final);
static void ansi_reset_parser(terminal_t *term);

/**
 * Scroll terminal up by one line. The line that falls off the top is copied
 * into the scrollback ring before being discarded.
 */
static void terminal_scroll(terminal_t *term)
{
    uint32_t row, col;

    if (term->scrollback != NULL) {
        for (col = 0; col < TERMINAL_COLS; col++) {
            term->scrollback[term->scrollback_head][col] = term->cells[0][col];
        }
        term->scrollback_head = (term->scrollback_head + 1) % TERMINAL_SCROLLBACK_LINES;
        if (term->scrollback_count < TERMINAL_SCROLLBACK_LINES) {
            term->scrollback_count++;
        }
    }

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
 * Resolve viewport row -> cell row. Returns NULL if the row is above the
 * oldest scrollback line (viewport showing pre-history; render blank).
 */
static const terminal_cell_t *viewport_row(const terminal_t *term, uint32_t row)
{
    int32_t logical = (int32_t)term->scrollback_count
                    - (int32_t)term->viewport_offset
                    + (int32_t)row;
    uint32_t ring_idx;

    if (logical < 0) {
        return NULL;
    }
    if (logical < (int32_t)term->scrollback_count) {
        if (term->scrollback == NULL) {
            return NULL;
        }
        if (term->scrollback_count < TERMINAL_SCROLLBACK_LINES) {
            ring_idx = (uint32_t)logical;
        } else {
            ring_idx = (term->scrollback_head + (uint32_t)logical)
                       % TERMINAL_SCROLLBACK_LINES;
        }
        return term->scrollback[ring_idx];
    }
    return term->cells[(uint32_t)logical - term->scrollback_count];
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

    win_width  = TERMINAL_WIN_WIDTH;
    win_height = TERMINAL_WIN_HEIGHT;

    term->scrollback = (terminal_cell_t (*)[TERMINAL_COLS])
        kmalloc(sizeof(terminal_cell_t) * TERMINAL_SCROLLBACK_LINES * TERMINAL_COLS);
    if (!term->scrollback) {
        klog_error("Failed to allocate scrollback");
        kfree(term);
        return NULL;
    }
    memset(term->scrollback, 0,
           sizeof(terminal_cell_t) * TERMINAL_SCROLLBACK_LINES * TERMINAL_COLS);

    /* Create window */
    term->window = window_create("Terminal", 100, 50, win_width, win_height,
                                  WINDOW_FLAG_VISIBLE);
    if (!term->window) {
        kfree(term->scrollback);
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

    /* Draw cells (live grid when viewport_offset == 0, else through scrollback) */
    for (row = 0; row < TERMINAL_ROWS; row++) {
        const terminal_cell_t *src = viewport_row(term, row);
        y = row * TERMINAL_CHAR_HEIGHT + TERMINAL_PAD_Y;
        for (col = 0; col < TERMINAL_COLS; col++) {
            x = col * TERMINAL_CHAR_WIDTH + TERMINAL_PAD_X;
            if (src == NULL) {
                window_putchar_large(win, x, y, ' ',
                                     term_colors[TERM_COLOR_WHITE],
                                     term_colors[TERM_COLOR_BLACK]);
                continue;
            }
            fg = term_colors[src[col].fg & 0x0F];
            bg = term_colors[src[col].bg & 0x0F];
            window_putchar_large(win, x, y, src[col].ch, fg, bg);
        }
    }

    /* Draw cursor only in live view; while paged back the cursor coords map
     * into off-screen live cells and would render at the wrong place. */
    if (term->viewport_offset == 0 &&
        term->cursor_visible && term->cursor_blink_state) {
        x = term->cursor_x * TERMINAL_CHAR_WIDTH + TERMINAL_PAD_X;
        y = term->cursor_y * TERMINAL_CHAR_HEIGHT + TERMINAL_PAD_Y;
        window_fill_rect(win, x, y, TERMINAL_CHAR_WIDTH, TERMINAL_CHAR_HEIGHT,
                         term_colors[TERM_COLOR_WHITE]);

        /* Draw character under cursor in inverse */
        if (term->cells[term->cursor_y][term->cursor_x].ch != ' ') {
            window_putchar_large(win, x, y,
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

    /* Null callbacks before destroy so any in-flight event finds NULL */
    win->on_paint = NULL;
    win->on_key = NULL;
    win->on_mouse = NULL;
    win->on_close = NULL;

    wm_unregister_window(win);
    window_destroy(win);

    if (term) {
        if (active_terminal == term) {
            active_terminal = NULL;
        }
        if (term->scrollback) {
            kfree(term->scrollback);
            term->scrollback = NULL;
        }
        kfree(term);
    }
}

/**
 * Reset the ANSI parser to ground state. Called on entry, on completion of a
 * sequence, and on any malformed input we want to discard.
 */
static void ansi_reset_parser(terminal_t *term)
{
    term->ansi_state       = ANSI_NORMAL;
    term->ansi_private     = false;
    term->ansi_seen_param  = false;
    term->ansi_param_count = 0;
    term->ansi_params[0]   = 0;
}

/**
 * Feed one byte to the ANSI parser. Caller must check that ansi_state is not
 * ANSI_NORMAL before calling. Final bytes invoke ansi_dispatch and reset the
 * parser; anything unexpected drops the in-flight sequence silently.
 */
static void ansi_feed(terminal_t *term, char c)
{
    if (term->ansi_state == ANSI_ESC) {
        if (c == '[') {
            term->ansi_state = ANSI_CSI;
            return;
        }
        /* Unknown two-byte escape; drop and resume. */
        ansi_reset_parser(term);
        return;
    }

    /* ANSI_CSI from here on. */
    if (c == '?' && !term->ansi_seen_param && term->ansi_param_count == 0) {
        term->ansi_private = true;
        return;
    }
    if (c >= '0' && c <= '9') {
        if (term->ansi_param_count < 8) {
            term->ansi_params[term->ansi_param_count] =
                (uint16_t)(term->ansi_params[term->ansi_param_count] * 10
                           + (uint16_t)(c - '0'));
            term->ansi_seen_param = true;
        }
        return;
    }
    if (c == ';') {
        if (term->ansi_param_count < 7) {
            term->ansi_param_count++;
            term->ansi_params[term->ansi_param_count] = 0;
        }
        return;
    }
    /* Final byte. Promote the current param to the count when at least one
     * digit was seen; otherwise count stays at zero so dispatchers can detect
     * "no params" and apply defaults. */
    if (term->ansi_seen_param) {
        term->ansi_param_count++;
    }
    ansi_dispatch(term, c);
    ansi_reset_parser(term);
}

/**
 * Run the side effect of a fully parsed CSI sequence. Unknown final bytes are
 * silently ignored so future sequences (bold, dim, save/restore) don't crash
 * the terminal — they just don't render.
 */
static void ansi_dispatch(terminal_t *term, char final)
{
    uint32_t row, col, i;
    uint16_t p;
    uint8_t  swap;

    if (term->ansi_private) {
        if (term->ansi_param_count >= 1 && term->ansi_params[0] == 25) {
            if (final == 'l') {
                term->cursor_visible = false;
            } else if (final == 'h') {
                term->cursor_visible = true;
            }
        }
        return;
    }

    switch (final) {
    case 'H':
    case 'f':
        row = (term->ansi_param_count >= 1 && term->ansi_params[0] > 0)
              ? (uint32_t)(term->ansi_params[0] - 1) : 0;
        col = (term->ansi_param_count >= 2 && term->ansi_params[1] > 0)
              ? (uint32_t)(term->ansi_params[1] - 1) : 0;
        if (row >= TERMINAL_ROWS) row = TERMINAL_ROWS - 1;
        if (col >= TERMINAL_COLS) col = TERMINAL_COLS - 1;
        term->cursor_y = row;
        term->cursor_x = col;
        break;

    case 'J':
        if (term->ansi_param_count == 0 || term->ansi_params[0] == 2) {
            terminal_clear(term);
        }
        break;

    case 'K':
        for (col = term->cursor_x; col < TERMINAL_COLS; col++) {
            term->cells[term->cursor_y][col].ch = ' ';
            term->cells[term->cursor_y][col].fg = term->current_fg;
            term->cells[term->cursor_y][col].bg = term->current_bg;
        }
        break;

    case 'm':
        if (term->ansi_param_count == 0) {
            term->current_fg = TERM_COLOR_WHITE;
            term->current_bg = TERM_COLOR_BLACK;
            break;
        }
        for (i = 0; i < term->ansi_param_count; i++) {
            p = term->ansi_params[i];
            if (p == 0) {
                term->current_fg = TERM_COLOR_WHITE;
                term->current_bg = TERM_COLOR_BLACK;
            } else if (p == 7) {
                swap = term->current_fg;
                term->current_fg = term->current_bg;
                term->current_bg = swap;
            } else if (p >= 30 && p <= 37) {
                term->current_fg = (uint8_t)(p - 30);
            } else if (p >= 40 && p <= 47) {
                term->current_bg = (uint8_t)(p - 40);
            } else if (p >= 90 && p <= 97) {
                term->current_fg = (uint8_t)(p - 90 + 8);
            } else if (p >= 100 && p <= 107) {
                term->current_bg = (uint8_t)(p - 100 + 8);
            }
            /* p == 1 (bold), p == 2 (dim), and other unhandled params are
             * silently ignored. */
        }
        break;

    default:
        break;
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

    /* Route ESC and in-flight escape sequences through the ANSI parser
     * instead of the cell printer. */
    if (term->ansi_state != ANSI_NORMAL) {
        ansi_feed(term, c);
        window_invalidate(term->window);
        return;
    }
    if (c == 0x1B) {
        term->ansi_state = ANSI_ESC;
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

    /* Handle clear specially — use terminal clear, not ANSI escape */
    if (strcmp(line, "clear") == 0) {
        terminal_clear(term);
        return;
    }

    /* Block commands that are unsafe in GUI terminal */
    if (strcmp(line, "edit") == 0 || strncmp(line, "edit ", 5) == 0 ||
        strcmp(line, "vi") == 0 || strncmp(line, "vi ", 3) == 0) {
        terminal_puts(term, "Editor not available in GUI terminal.\n");
        terminal_puts(term, "Use text mode (make run) for the editor.\n");
        return;
    }
    if (strcmp(line, "startx") == 0) {
        terminal_puts(term, "Already in graphical mode.\n");
        return;
    }
    if (strcmp(line, "exit") == 0) {
        terminal_puts(term, "Use the window close button to close terminal.\n");
        return;
    }

    /* Set active terminal and redirect kprintf output. shell_run_line
     * supports `|` pipes by transiently rerouting kprintf into a ring
     * buffer for non-final stages, then restores it (to the terminal hook
     * we install here) for the final stage. */
    active_terminal = term;
    kprintf_output_hook = terminal_kprintf_hook;

    shell_run_line(line);

    /* Restore normal UART output */
    kprintf_output_hook = NULL;
    (void)argc;
    (void)argv;
}

/**
 * Handle key input
 */
void terminal_handle_key(terminal_t *term, key_event_t *key)
{
    uint32_t step;

    if (!term) {
        return;
    }

    /* Scrollback navigation. Page Up moves the viewport into history, Page
     * Down moves it back toward live; we keep one row of overlap so the user
     * doesn't lose context between pages. */
    if (key->keycode == KEY_PAGE_UP) {
        step = (TERMINAL_ROWS > 1) ? (TERMINAL_ROWS - 1) : 1;
        if (term->viewport_offset + step > term->scrollback_count) {
            term->viewport_offset = term->scrollback_count;
        } else {
            term->viewport_offset += step;
        }
        window_invalidate(term->window);
        return;
    }
    if (key->keycode == KEY_PAGE_DOWN) {
        step = (TERMINAL_ROWS > 1) ? (TERMINAL_ROWS - 1) : 1;
        if (term->viewport_offset > step) {
            term->viewport_offset -= step;
        } else {
            term->viewport_offset = 0;
        }
        window_invalidate(term->window);
        return;
    }

    /* Any other key implies the user wants to interact with live; snap back
     * before processing, otherwise typed input lands in invisible cells. */
    if (term->viewport_offset != 0) {
        term->viewport_offset = 0;
        window_invalidate(term->window);
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
