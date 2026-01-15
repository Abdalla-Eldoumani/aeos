/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/kernel/editor.c
 * Description: Vim-like text editor implementation
 * ============================================================================ */

#include <aeos/editor.h>
#include <aeos/kprintf.h>
#include <aeos/uart.h>
#include <aeos/string.h>
#include <aeos/heap.h>
#include <aeos/vfs.h>
#include <aeos/timer.h>

/* ============================================================================
 * ANSI Escape Codes for Terminal Control
 * ============================================================================ */

#define ESC_CLEAR_SCREEN    "\x1b[2J"
#define ESC_CURSOR_HOME     "\x1b[H"
#define ESC_CLEAR_LINE      "\x1b[K"
#define ESC_CURSOR_HIDE     "\x1b[?25l"
#define ESC_CURSOR_SHOW     "\x1b[?25h"
#define ESC_REVERSE_VIDEO   "\x1b[7m"
#define ESC_RESET_ATTR      "\x1b[m"
#define ESC_BOLD            "\x1b[1m"
#define ESC_DIM             "\x1b[2m"

/* Move cursor to row, col (1-based) */
static void term_move_cursor(int row, int col)
{
    kprintf("\x1b[%d;%dH", row + 1, col + 1);
}

/* ============================================================================
 * Line Buffer Management
 * ============================================================================ */

/**
 * Initialize a line
 */
static int line_init(editor_line_t *line)
{
    line->capacity = 64;
    line->chars = (char *)kmalloc(line->capacity);
    if (line->chars == NULL) {
        return -1;
    }
    line->chars[0] = '\0';
    line->len = 0;
    return 0;
}

/**
 * Free a line
 */
static void line_free(editor_line_t *line)
{
    if (line->chars != NULL) {
        kfree(line->chars);
        line->chars = NULL;
    }
    line->len = 0;
    line->capacity = 0;
}

/**
 * Ensure line has enough capacity
 */
static int line_grow(editor_line_t *line, size_t needed)
{
    if (needed <= line->capacity) {
        return 0;
    }

    size_t new_cap = line->capacity * 2;
    while (new_cap < needed) {
        new_cap *= 2;
    }

    char *new_chars = (char *)kmalloc(new_cap);
    if (new_chars == NULL) {
        return -1;
    }

    memcpy(new_chars, line->chars, line->len + 1);
    kfree(line->chars);
    line->chars = new_chars;
    line->capacity = new_cap;
    return 0;
}

/**
 * Insert character at position
 */
static int line_insert_char(editor_line_t *line, int pos, char c)
{
    if (pos < 0 || (size_t)pos > line->len) {
        return -1;
    }

    if (line_grow(line, line->len + 2) < 0) {
        return -1;
    }

    /* Shift characters right */
    memmove(&line->chars[pos + 1], &line->chars[pos], line->len - pos + 1);
    line->chars[pos] = c;
    line->len++;
    return 0;
}

/**
 * Delete character at position
 */
static int line_delete_char(editor_line_t *line, int pos)
{
    if (pos < 0 || (size_t)pos >= line->len) {
        return -1;
    }

    /* Shift characters left */
    memmove(&line->chars[pos], &line->chars[pos + 1], line->len - pos);
    line->len--;
    return 0;
}

/**
 * Set line content
 */
static int line_set(editor_line_t *line, const char *str, size_t len)
{
    if (line_grow(line, len + 1) < 0) {
        return -1;
    }

    memcpy(line->chars, str, len);
    line->chars[len] = '\0';
    line->len = len;
    return 0;
}

/* ============================================================================
 * Editor Buffer Management
 * ============================================================================ */

/**
 * Add a new line to the editor
 */
static int editor_add_line(editor_t *ed, int pos, const char *text, size_t len)
{
    /* Grow lines array if needed */
    if (ed->num_lines >= ed->lines_capacity) {
        int new_cap = ed->lines_capacity * 2;
        editor_line_t *new_lines = (editor_line_t *)kmalloc(new_cap * sizeof(editor_line_t));
        if (new_lines == NULL) {
            return -1;
        }
        memcpy(new_lines, ed->lines, ed->num_lines * sizeof(editor_line_t));
        kfree(ed->lines);
        ed->lines = new_lines;
        ed->lines_capacity = new_cap;
    }

    /* Shift lines down */
    if (pos < ed->num_lines) {
        memmove(&ed->lines[pos + 1], &ed->lines[pos],
                (ed->num_lines - pos) * sizeof(editor_line_t));
    }

    /* Initialize new line */
    if (line_init(&ed->lines[pos]) < 0) {
        return -1;
    }

    if (text != NULL && len > 0) {
        if (line_set(&ed->lines[pos], text, len) < 0) {
            return -1;
        }
    }

    ed->num_lines++;
    return 0;
}

/**
 * Delete a line from the editor
 */
static int editor_delete_line(editor_t *ed, int pos)
{
    if (pos < 0 || pos >= ed->num_lines) {
        return -1;
    }

    line_free(&ed->lines[pos]);

    /* Shift lines up */
    if (pos < ed->num_lines - 1) {
        memmove(&ed->lines[pos], &ed->lines[pos + 1],
                (ed->num_lines - pos - 1) * sizeof(editor_line_t));
    }

    ed->num_lines--;

    /* Ensure at least one empty line */
    if (ed->num_lines == 0) {
        editor_add_line(ed, 0, NULL, 0);
    }

    return 0;
}

/* ============================================================================
 * Input Handling
 * ============================================================================ */

/**
 * Check if UART has data available (non-blocking check)
 */
static bool uart_has_data(void)
{
    /* PL011 UART Flag Register - check RX FIFO not empty */
    volatile uint32_t *fr = (volatile uint32_t *)0x09000018;
    return (*fr & (1 << 4)) == 0;  /* RXFE = 0 means data available */
}

/**
 * Read a key with escape sequence handling
 */
int editor_read_key(void)
{
    char c = uart_getc();

    /* Handle escape sequences */
    if (c == '\x1b') {
        /* Wait a bit for more characters */
        timer_delay_ms(20);

        if (!uart_has_data()) {
            return KEY_ESCAPE;
        }

        char seq[3];
        seq[0] = uart_getc();

        if (seq[0] == '[') {
            if (!uart_has_data()) {
                return KEY_ESCAPE;
            }

            seq[1] = uart_getc();

            /* Arrow keys and simple sequences */
            switch (seq[1]) {
                case 'A': return KEY_UP;
                case 'B': return KEY_DOWN;
                case 'C': return KEY_RIGHT;
                case 'D': return KEY_LEFT;
                case 'H': return KEY_HOME;
                case 'F': return KEY_END;
            }

            /* Extended sequences: ESC [ n ~ */
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (uart_has_data()) {
                    seq[2] = uart_getc();
                    if (seq[2] == '~') {
                        switch (seq[1]) {
                            case '1': return KEY_HOME;
                            case '3': return KEY_DELETE;
                            case '4': return KEY_END;
                            case '5': return KEY_PAGE_UP;
                            case '6': return KEY_PAGE_DOWN;
                        }
                    }
                }
            }
        }

        return KEY_ESCAPE;
    }

    /* Handle backspace (both DEL and BS) */
    if (c == 127 || c == 8) {
        return KEY_BACKSPACE;
    }

    return c;
}

/* ============================================================================
 * Screen Drawing
 * ============================================================================ */

/**
 * Draw the editor screen
 */
void editor_refresh_screen(editor_t *ed)
{
    int row, col;
    int line_num_width = 4;  /* Width for line numbers */

    /* Hide cursor during redraw */
    kprintf(ESC_CURSOR_HIDE);

    /* Move to top-left */
    kprintf(ESC_CURSOR_HOME);

    /* Draw each row */
    for (row = 0; row < EDITOR_TERM_ROWS - 2; row++) {
        int file_row = row + ed->scroll_row;

        /* Clear line */
        kprintf(ESC_CLEAR_LINE);

        if (file_row < ed->num_lines) {
            /* Draw line number */
            kprintf(ESC_DIM);
            kprintf("%3d ", file_row + 1);
            kprintf(ESC_RESET_ATTR);

            /* Draw line content */
            editor_line_t *line = &ed->lines[file_row];
            int start_col = ed->scroll_col;
            int draw_len = EDITOR_TERM_COLS - line_num_width;

            if (start_col < (int)line->len) {
                int avail = (int)line->len - start_col;
                if (avail > draw_len) {
                    avail = draw_len;
                }

                /* Check for visual mode highlighting */
                if (ed->mode == MODE_VISUAL) {
                    /* Simple highlighting - just reverse video the selected region */
                    int sel_start = ed->selection.start_col;
                    int sel_end = ed->cursor_col;
                    if (ed->selection.start_row == file_row ||
                        (file_row > ed->selection.start_row && file_row < ed->cursor_row) ||
                        (file_row < ed->selection.start_row && file_row > ed->cursor_row)) {
                        kprintf(ESC_REVERSE_VIDEO);
                    }
                    (void)sel_start;
                    (void)sel_end;
                }

                /* Print the visible portion of the line */
                for (int i = 0; i < avail; i++) {
                    char ch = line->chars[start_col + i];
                    if (ch >= 32 && ch < 127) {
                        uart_putc(ch);
                    } else if (ch == '\t') {
                        uart_putc(' ');  /* Render tabs as spaces */
                    } else {
                        uart_putc('?');  /* Non-printable */
                    }
                }

                if (ed->mode == MODE_VISUAL) {
                    kprintf(ESC_RESET_ATTR);
                }
            }
        } else {
            /* Empty line marker */
            kprintf(ESC_DIM "~" ESC_RESET_ATTR);
        }

        kprintf("\r\n");
    }

    /* Draw status bar */
    kprintf(ESC_REVERSE_VIDEO);

    /* Left side: mode and filename */
    const char *mode_str;
    switch (ed->mode) {
        case MODE_INSERT: mode_str = "INSERT"; break;
        case MODE_VISUAL: mode_str = "VISUAL"; break;
        case MODE_EX:     mode_str = "EX"; break;
        default:          mode_str = "COMMAND"; break;
    }

    int left_len = kprintf(" %s | %.40s%s",
                           mode_str,
                           ed->filename[0] ? ed->filename : "[No Name]",
                           ed->modified ? " [+]" : "");

    /* Right side: position */
    char pos_str[32];
    int pos_len = 0;
    pos_len = kprintf(" %d:%d ", ed->cursor_row + 1, ed->cursor_col + 1);

    /* Pad middle */
    for (col = left_len; col < EDITOR_TERM_COLS - pos_len; col++) {
        uart_putc(' ');
    }

    (void)pos_str;

    kprintf(ESC_RESET_ATTR "\r\n");

    /* Draw message line or ex command */
    kprintf(ESC_CLEAR_LINE);
    if (ed->mode == MODE_EX) {
        kprintf(":%s", ed->ex_command);
    } else if (ed->status_msg[0]) {
        kprintf("%s", ed->status_msg);
    }

    /* Position cursor */
    int cursor_screen_row = ed->cursor_row - ed->scroll_row;
    int cursor_screen_col = line_num_width + ed->cursor_col - ed->scroll_col;

    if (cursor_screen_row >= 0 && cursor_screen_row < EDITOR_TERM_ROWS - 2) {
        term_move_cursor(cursor_screen_row, cursor_screen_col);
    }

    /* Show cursor */
    kprintf(ESC_CURSOR_SHOW);
}

/* ============================================================================
 * Cursor Movement
 * ============================================================================ */

/**
 * Move cursor up
 */
static void cursor_up(editor_t *ed)
{
    if (ed->cursor_row > 0) {
        ed->cursor_row--;
        /* Clamp column to line length */
        if (ed->cursor_col > (int)ed->lines[ed->cursor_row].len) {
            ed->cursor_col = (int)ed->lines[ed->cursor_row].len;
        }
    }
}

/**
 * Move cursor down
 */
static void cursor_down(editor_t *ed)
{
    if (ed->cursor_row < ed->num_lines - 1) {
        ed->cursor_row++;
        /* Clamp column to line length */
        if (ed->cursor_col > (int)ed->lines[ed->cursor_row].len) {
            ed->cursor_col = (int)ed->lines[ed->cursor_row].len;
        }
    }
}

/**
 * Move cursor left
 */
static void cursor_left(editor_t *ed)
{
    if (ed->cursor_col > 0) {
        ed->cursor_col--;
    } else if (ed->cursor_row > 0) {
        /* Move to end of previous line */
        ed->cursor_row--;
        ed->cursor_col = (int)ed->lines[ed->cursor_row].len;
    }
}

/**
 * Move cursor right
 */
static void cursor_right(editor_t *ed)
{
    editor_line_t *line = &ed->lines[ed->cursor_row];
    if (ed->cursor_col < (int)line->len) {
        ed->cursor_col++;
    } else if (ed->cursor_row < ed->num_lines - 1) {
        /* Move to start of next line */
        ed->cursor_row++;
        ed->cursor_col = 0;
    }
}

/**
 * Move cursor to start of line
 */
static void cursor_home(editor_t *ed)
{
    ed->cursor_col = 0;
}

/**
 * Move cursor to end of line
 */
static void cursor_end(editor_t *ed)
{
    ed->cursor_col = (int)ed->lines[ed->cursor_row].len;
}

/**
 * Move cursor to next word
 */
static void cursor_word_forward(editor_t *ed)
{
    editor_line_t *line = &ed->lines[ed->cursor_row];

    /* Skip current word */
    while (ed->cursor_col < (int)line->len &&
           line->chars[ed->cursor_col] != ' ') {
        ed->cursor_col++;
    }

    /* Skip spaces */
    while (ed->cursor_col < (int)line->len &&
           line->chars[ed->cursor_col] == ' ') {
        ed->cursor_col++;
    }

    /* If at end of line, go to next line */
    if (ed->cursor_col >= (int)line->len && ed->cursor_row < ed->num_lines - 1) {
        ed->cursor_row++;
        ed->cursor_col = 0;
    }
}

/**
 * Move cursor to previous word
 */
static void cursor_word_backward(editor_t *ed)
{
    /* If at start of line, go to previous line */
    if (ed->cursor_col == 0 && ed->cursor_row > 0) {
        ed->cursor_row--;
        ed->cursor_col = (int)ed->lines[ed->cursor_row].len;
    }

    if (ed->cursor_col > 0) {
        ed->cursor_col--;
    }

    editor_line_t *line = &ed->lines[ed->cursor_row];

    /* Skip spaces */
    while (ed->cursor_col > 0 && line->chars[ed->cursor_col] == ' ') {
        ed->cursor_col--;
    }

    /* Skip word */
    while (ed->cursor_col > 0 && line->chars[ed->cursor_col - 1] != ' ') {
        ed->cursor_col--;
    }
}

/**
 * Update scroll position to keep cursor visible
 */
static void editor_scroll(editor_t *ed)
{
    /* Vertical scroll */
    if (ed->cursor_row < ed->scroll_row) {
        ed->scroll_row = ed->cursor_row;
    }
    if (ed->cursor_row >= ed->scroll_row + EDITOR_TERM_ROWS - 2) {
        ed->scroll_row = ed->cursor_row - EDITOR_TERM_ROWS + 3;
    }

    /* Horizontal scroll */
    int line_num_width = 4;
    int visible_cols = EDITOR_TERM_COLS - line_num_width;

    if (ed->cursor_col < ed->scroll_col) {
        ed->scroll_col = ed->cursor_col;
    }
    if (ed->cursor_col >= ed->scroll_col + visible_cols) {
        ed->scroll_col = ed->cursor_col - visible_cols + 1;
    }
}

/* ============================================================================
 * Text Editing Operations
 * ============================================================================ */

/**
 * Insert a character at cursor
 */
static void editor_insert_char(editor_t *ed, char c)
{
    editor_line_t *line = &ed->lines[ed->cursor_row];

    if (line_insert_char(line, ed->cursor_col, c) == 0) {
        ed->cursor_col++;
        ed->modified = true;
    }
}

/**
 * Insert a new line at cursor (split line)
 */
static void editor_insert_newline(editor_t *ed)
{
    editor_line_t *line = &ed->lines[ed->cursor_row];

    /* Text after cursor goes to new line */
    const char *after = &line->chars[ed->cursor_col];
    size_t after_len = line->len - ed->cursor_col;

    /* Insert new line */
    if (editor_add_line(ed, ed->cursor_row + 1, after, after_len) < 0) {
        return;
    }

    /* Truncate current line */
    line->chars[ed->cursor_col] = '\0';
    line->len = ed->cursor_col;

    /* Move cursor to start of new line */
    ed->cursor_row++;
    ed->cursor_col = 0;
    ed->modified = true;
}

/**
 * Delete character at cursor
 */
static void editor_delete_char(editor_t *ed)
{
    editor_line_t *line = &ed->lines[ed->cursor_row];

    if (ed->cursor_col < (int)line->len) {
        line_delete_char(line, ed->cursor_col);
        ed->modified = true;
    } else if (ed->cursor_row < ed->num_lines - 1) {
        /* Join with next line */
        editor_line_t *next = &ed->lines[ed->cursor_row + 1];
        if (line_grow(line, line->len + next->len + 1) == 0) {
            memcpy(&line->chars[line->len], next->chars, next->len + 1);
            line->len += next->len;
            editor_delete_line(ed, ed->cursor_row + 1);
            ed->modified = true;
        }
    }
}

/**
 * Delete character before cursor (backspace)
 */
static void editor_backspace(editor_t *ed)
{
    if (ed->cursor_col > 0) {
        ed->cursor_col--;
        editor_delete_char(ed);
    } else if (ed->cursor_row > 0) {
        /* Join with previous line */
        ed->cursor_row--;
        ed->cursor_col = (int)ed->lines[ed->cursor_row].len;
        editor_delete_char(ed);
    }
}

/**
 * Delete current line
 */
static void editor_delete_current_line(editor_t *ed)
{
    /* Yank the line first */
    editor_line_t *line = &ed->lines[ed->cursor_row];
    size_t copy_len = line->len < EDITOR_MAX_YANK - 1 ? line->len : EDITOR_MAX_YANK - 1;
    memcpy(ed->yank_buffer, line->chars, copy_len);
    ed->yank_buffer[copy_len] = '\0';
    ed->yank_len = copy_len;
    ed->yank_is_line = true;

    /* Delete the line */
    editor_delete_line(ed, ed->cursor_row);

    /* Adjust cursor */
    if (ed->cursor_row >= ed->num_lines) {
        ed->cursor_row = ed->num_lines - 1;
    }
    ed->cursor_col = 0;
    ed->modified = true;
}

/**
 * Yank (copy) current line
 */
static void editor_yank_line(editor_t *ed)
{
    editor_line_t *line = &ed->lines[ed->cursor_row];
    size_t copy_len = line->len < EDITOR_MAX_YANK - 1 ? line->len : EDITOR_MAX_YANK - 1;
    memcpy(ed->yank_buffer, line->chars, copy_len);
    ed->yank_buffer[copy_len] = '\0';
    ed->yank_len = copy_len;
    ed->yank_is_line = true;

    editor_set_status(ed, "Yanked line");
}

/**
 * Paste yanked text after cursor
 */
static void editor_paste_after(editor_t *ed)
{
    if (ed->yank_len == 0) {
        editor_set_status(ed, "Nothing to paste");
        return;
    }

    if (ed->yank_is_line) {
        /* Paste as new line below */
        if (editor_add_line(ed, ed->cursor_row + 1, ed->yank_buffer, ed->yank_len) == 0) {
            ed->cursor_row++;
            ed->cursor_col = 0;
            ed->modified = true;
        }
    } else {
        /* Paste inline */
        editor_line_t *line = &ed->lines[ed->cursor_row];
        for (size_t i = 0; i < ed->yank_len; i++) {
            if (line_insert_char(line, ed->cursor_col + (int)i, ed->yank_buffer[i]) < 0) {
                break;
            }
        }
        ed->cursor_col += (int)ed->yank_len;
        ed->modified = true;
    }
}

/**
 * Paste yanked text before cursor
 */
static void editor_paste_before(editor_t *ed)
{
    if (ed->yank_len == 0) {
        editor_set_status(ed, "Nothing to paste");
        return;
    }

    if (ed->yank_is_line) {
        /* Paste as new line above */
        if (editor_add_line(ed, ed->cursor_row, ed->yank_buffer, ed->yank_len) == 0) {
            ed->cursor_col = 0;
            ed->modified = true;
        }
    } else {
        /* Paste inline at cursor */
        editor_line_t *line = &ed->lines[ed->cursor_row];
        for (size_t i = 0; i < ed->yank_len; i++) {
            if (line_insert_char(line, ed->cursor_col + (int)i, ed->yank_buffer[i]) < 0) {
                break;
            }
        }
        ed->modified = true;
    }
}

/* ============================================================================
 * Search
 * ============================================================================ */

/**
 * Find next occurrence of search pattern
 */
static bool editor_find_next(editor_t *ed, bool forward)
{
    if (ed->search_pattern[0] == '\0') {
        editor_set_status(ed, "No search pattern");
        return false;
    }

    int start_row = ed->cursor_row;
    int start_col = ed->cursor_col + (forward ? 1 : -1);

    /* Search through lines */
    for (int i = 0; i < ed->num_lines; i++) {
        int row;
        if (forward) {
            row = (start_row + i) % ed->num_lines;
        } else {
            row = (start_row - i + ed->num_lines) % ed->num_lines;
        }

        editor_line_t *line = &ed->lines[row];
        char *found = NULL;

        if (row == start_row && i == 0) {
            /* Start from current position */
            if (forward && start_col < (int)line->len) {
                found = strstr(&line->chars[start_col], ed->search_pattern);
            }
        } else {
            found = strstr(line->chars, ed->search_pattern);
        }

        if (found != NULL) {
            ed->cursor_row = row;
            ed->cursor_col = (int)(found - line->chars);
            ed->search_row = row;
            ed->search_col = ed->cursor_col;
            return true;
        }
    }

    editor_set_status(ed, "Pattern not found: %s", ed->search_pattern);
    return false;
}

/* ============================================================================
 * Ex Command Processing
 * ============================================================================ */

/**
 * Process ex command
 */
static void editor_process_ex_command(editor_t *ed)
{
    char *cmd = ed->ex_command;

    /* :w - write file */
    if (strcmp(cmd, "w") == 0) {
        if (editor_save(ed) == 0) {
            editor_set_status(ed, "Written");
        }
    }
    /* :q - quit */
    else if (strcmp(cmd, "q") == 0) {
        if (ed->modified) {
            editor_set_status(ed, "No write since last change (use :q! to force)");
        } else {
            ed->should_quit = true;
        }
    }
    /* :q! - force quit */
    else if (strcmp(cmd, "q!") == 0) {
        ed->should_quit = true;
    }
    /* :wq - write and quit */
    else if (strcmp(cmd, "wq") == 0) {
        if (editor_save(ed) == 0) {
            ed->should_quit = true;
        }
    }
    /* :<number> - go to line */
    else if (cmd[0] >= '0' && cmd[0] <= '9') {
        int line_num = 0;
        for (int i = 0; cmd[i] >= '0' && cmd[i] <= '9'; i++) {
            line_num = line_num * 10 + (cmd[i] - '0');
        }
        if (line_num > 0 && line_num <= ed->num_lines) {
            ed->cursor_row = line_num - 1;
            ed->cursor_col = 0;
        } else {
            editor_set_status(ed, "Invalid line number");
        }
    }
    else {
        editor_set_status(ed, "Unknown command: %s", cmd);
    }

    /* Clear ex command and return to command mode */
    ed->ex_command[0] = '\0';
    ed->ex_len = 0;
    ed->mode = MODE_COMMAND;
}

/* ============================================================================
 * Key Processing by Mode
 * ============================================================================ */

/**
 * Process key in COMMAND mode
 */
static void process_command_mode(editor_t *ed, int key)
{
    static int g_pending = 0;  /* For 'gg' command */
    static int d_pending = 0;  /* For 'dd' command */
    static int y_pending = 0;  /* For 'yy' command */

    /* Check for pending commands */
    if (g_pending) {
        g_pending = 0;
        if (key == 'g') {
            /* gg - go to first line */
            ed->cursor_row = 0;
            ed->cursor_col = 0;
            return;
        }
    }

    if (d_pending) {
        d_pending = 0;
        if (key == 'd') {
            /* dd - delete line */
            editor_delete_current_line(ed);
            return;
        }
    }

    if (y_pending) {
        y_pending = 0;
        if (key == 'y') {
            /* yy - yank line */
            editor_yank_line(ed);
            return;
        }
    }

    switch (key) {
        /* Cursor movement */
        case 'h':
        case KEY_LEFT:
            cursor_left(ed);
            break;

        case 'j':
        case KEY_DOWN:
            cursor_down(ed);
            break;

        case 'k':
        case KEY_UP:
            cursor_up(ed);
            break;

        case 'l':
        case KEY_RIGHT:
            cursor_right(ed);
            break;

        case '0':
        case KEY_HOME:
            cursor_home(ed);
            break;

        case '$':
        case KEY_END:
            cursor_end(ed);
            break;

        case 'w':
            cursor_word_forward(ed);
            break;

        case 'b':
            cursor_word_backward(ed);
            break;

        case 'g':
            g_pending = 1;
            break;

        case 'G':
            /* G - go to last line */
            ed->cursor_row = ed->num_lines - 1;
            ed->cursor_col = 0;
            break;

        /* Mode switching */
        case 'i':
            ed->mode = MODE_INSERT;
            editor_set_status(ed, "-- INSERT --");
            break;

        case 'a':
            /* Append after cursor */
            ed->mode = MODE_INSERT;
            if (ed->cursor_col < (int)ed->lines[ed->cursor_row].len) {
                ed->cursor_col++;
            }
            editor_set_status(ed, "-- INSERT --");
            break;

        case 'o':
            /* Open line below */
            editor_add_line(ed, ed->cursor_row + 1, NULL, 0);
            ed->cursor_row++;
            ed->cursor_col = 0;
            ed->mode = MODE_INSERT;
            ed->modified = true;
            editor_set_status(ed, "-- INSERT --");
            break;

        case 'O':
            /* Open line above */
            editor_add_line(ed, ed->cursor_row, NULL, 0);
            ed->cursor_col = 0;
            ed->mode = MODE_INSERT;
            ed->modified = true;
            editor_set_status(ed, "-- INSERT --");
            break;

        case 'v':
            ed->mode = MODE_VISUAL;
            ed->selection.start_row = ed->cursor_row;
            ed->selection.start_col = ed->cursor_col;
            editor_set_status(ed, "-- VISUAL --");
            break;

        case ':':
            ed->mode = MODE_EX;
            ed->ex_command[0] = '\0';
            ed->ex_len = 0;
            break;

        case '/':
            /* Enter search mode */
            ed->mode = MODE_EX;
            ed->ex_command[0] = '\0';
            ed->ex_len = 0;
            ed->search_active = true;
            break;

        /* Editing */
        case 'x':
        case KEY_DELETE:
            editor_delete_char(ed);
            break;

        case 'd':
            d_pending = 1;
            break;

        case 'y':
            y_pending = 1;
            break;

        case 'p':
            editor_paste_after(ed);
            break;

        case 'P':
            editor_paste_before(ed);
            break;

        /* Search */
        case 'n':
            editor_find_next(ed, true);
            break;

        case 'N':
            editor_find_next(ed, false);
            break;

        default:
            break;
    }
}

/**
 * Process key in INSERT mode
 */
static void process_insert_mode(editor_t *ed, int key)
{
    switch (key) {
        case KEY_ESCAPE:
            ed->mode = MODE_COMMAND;
            if (ed->cursor_col > 0) {
                ed->cursor_col--;
            }
            ed->status_msg[0] = '\0';
            break;

        case KEY_ENTER:
            editor_insert_newline(ed);
            break;

        case KEY_BACKSPACE:
            editor_backspace(ed);
            break;

        case KEY_DELETE:
            editor_delete_char(ed);
            break;

        case KEY_UP:
            cursor_up(ed);
            break;

        case KEY_DOWN:
            cursor_down(ed);
            break;

        case KEY_LEFT:
            cursor_left(ed);
            break;

        case KEY_RIGHT:
            cursor_right(ed);
            break;

        case KEY_HOME:
            cursor_home(ed);
            break;

        case KEY_END:
            cursor_end(ed);
            break;

        default:
            /* Insert printable characters */
            if (key >= 32 && key < 127) {
                editor_insert_char(ed, (char)key);
            } else if (key == '\t') {
                /* Insert tab as spaces */
                for (int i = 0; i < 4; i++) {
                    editor_insert_char(ed, ' ');
                }
            }
            break;
    }
}

/**
 * Process key in VISUAL mode
 */
static void process_visual_mode(editor_t *ed, int key)
{
    switch (key) {
        case KEY_ESCAPE:
            ed->mode = MODE_COMMAND;
            ed->status_msg[0] = '\0';
            break;

        /* Movement extends selection */
        case 'h':
        case KEY_LEFT:
            cursor_left(ed);
            break;

        case 'j':
        case KEY_DOWN:
            cursor_down(ed);
            break;

        case 'k':
        case KEY_UP:
            cursor_up(ed);
            break;

        case 'l':
        case KEY_RIGHT:
            cursor_right(ed);
            break;

        case 'y':
            /* Yank selection (simplified - just yank current line) */
            editor_yank_line(ed);
            ed->mode = MODE_COMMAND;
            break;

        case 'd':
            /* Delete selection (simplified - delete current line) */
            editor_delete_current_line(ed);
            ed->mode = MODE_COMMAND;
            break;

        default:
            break;
    }
}

/**
 * Process key in EX mode
 */
static void process_ex_mode(editor_t *ed, int key)
{
    switch (key) {
        case KEY_ESCAPE:
            ed->mode = MODE_COMMAND;
            ed->ex_command[0] = '\0';
            ed->ex_len = 0;
            ed->search_active = false;
            break;

        case KEY_ENTER:
            if (ed->search_active) {
                /* Execute search */
                strncpy(ed->search_pattern, ed->ex_command, EDITOR_MAX_SEARCH - 1);
                ed->search_pattern[EDITOR_MAX_SEARCH - 1] = '\0';
                ed->search_active = false;
                ed->mode = MODE_COMMAND;
                editor_find_next(ed, true);
            } else {
                editor_process_ex_command(ed);
            }
            ed->ex_command[0] = '\0';
            ed->ex_len = 0;
            break;

        case KEY_BACKSPACE:
            if (ed->ex_len > 0) {
                ed->ex_len--;
                ed->ex_command[ed->ex_len] = '\0';
            } else {
                ed->mode = MODE_COMMAND;
                ed->search_active = false;
            }
            break;

        default:
            /* Add character to command */
            if (key >= 32 && key < 127 && ed->ex_len < EDITOR_MAX_EX_CMD - 1) {
                ed->ex_command[ed->ex_len++] = (char)key;
                ed->ex_command[ed->ex_len] = '\0';
            }
            break;
    }
}

/**
 * Process a key in current mode
 */
void editor_process_key(editor_t *ed, int key)
{
    /* Clear status message on any key (except in ex mode) */
    if (ed->mode != MODE_EX) {
        ed->status_msg[0] = '\0';
    }

    switch (ed->mode) {
        case MODE_COMMAND:
            process_command_mode(ed, key);
            break;

        case MODE_INSERT:
            process_insert_mode(ed, key);
            break;

        case MODE_VISUAL:
            process_visual_mode(ed, key);
            break;

        case MODE_EX:
            process_ex_mode(ed, key);
            break;
    }

    /* Update scroll position */
    editor_scroll(ed);
}

/* ============================================================================
 * File Operations
 * ============================================================================ */

/**
 * Open a file
 */
int editor_open(editor_t *ed, const char *filename)
{
    int fd;
    char buffer[512];
    ssize_t bytes_read;
    char line_buf[EDITOR_MAX_LINE_LEN];
    int line_pos = 0;

    strncpy(ed->filename, filename, EDITOR_MAX_FILENAME - 1);
    ed->filename[EDITOR_MAX_FILENAME - 1] = '\0';

    fd = vfs_open(filename, O_RDONLY, 0);
    if (fd < 0) {
        /* New file */
        ed->new_file = true;
        return 0;
    }

    /* Read file content */
    while ((bytes_read = vfs_read(fd, buffer, sizeof(buffer))) > 0) {
        for (int i = 0; i < bytes_read; i++) {
            char c = buffer[i];

            if (c == '\n' || c == '\r') {
                /* End of line */
                line_buf[line_pos] = '\0';
                editor_add_line(ed, ed->num_lines, line_buf, line_pos);
                line_pos = 0;

                /* Skip \r\n pair */
                if (c == '\r' && i + 1 < bytes_read && buffer[i + 1] == '\n') {
                    i++;
                }
            } else if (line_pos < EDITOR_MAX_LINE_LEN - 1) {
                line_buf[line_pos++] = c;
            }
        }
    }

    /* Add last line if file doesn't end with newline */
    if (line_pos > 0) {
        line_buf[line_pos] = '\0';
        editor_add_line(ed, ed->num_lines, line_buf, line_pos);
    }

    vfs_close(fd);

    /* Remove the initial empty line if we loaded content */
    if (ed->num_lines > 1 && ed->lines[0].len == 0) {
        editor_delete_line(ed, 0);
    }

    return 0;
}

/**
 * Save editor buffer to file
 */
int editor_save(editor_t *ed)
{
    int fd;
    int row;

    if (ed->filename[0] == '\0') {
        editor_set_status(ed, "No filename");
        return -1;
    }

    fd = vfs_open(ed->filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        editor_set_status(ed, "Cannot save file");
        return -1;
    }

    /* Write each line */
    for (row = 0; row < ed->num_lines; row++) {
        editor_line_t *line = &ed->lines[row];
        if (line->len > 0) {
            vfs_write(fd, line->chars, line->len);
        }
        vfs_write(fd, "\n", 1);
    }

    vfs_close(fd);
    ed->modified = false;
    ed->new_file = false;

    editor_set_status(ed, "\"%s\" written, %d lines", ed->filename, ed->num_lines);
    return 0;
}

/* ============================================================================
 * Editor Lifecycle
 * ============================================================================ */

/**
 * Set status message
 */
void editor_set_status(editor_t *ed, const char *fmt, ...)
{
    /* Simple implementation - just copy first arg */
    strncpy(ed->status_msg, fmt, sizeof(ed->status_msg) - 1);
    ed->status_msg[sizeof(ed->status_msg) - 1] = '\0';
}

/**
 * Initialize editor
 */
int editor_init(editor_t *ed, const char *filename)
{
    /* Clear everything */
    memset(ed, 0, sizeof(editor_t));

    /* Allocate initial line array */
    ed->lines_capacity = 64;
    ed->lines = (editor_line_t *)kmalloc(ed->lines_capacity * sizeof(editor_line_t));
    if (ed->lines == NULL) {
        return -1;
    }

    /* Add one empty line */
    if (editor_add_line(ed, 0, NULL, 0) < 0) {
        kfree(ed->lines);
        return -1;
    }

    ed->mode = MODE_COMMAND;

    /* Open file if specified */
    if (filename != NULL && filename[0] != '\0') {
        editor_open(ed, filename);
    }

    return 0;
}

/**
 * Cleanup editor
 */
void editor_cleanup(editor_t *ed)
{
    /* Free all lines */
    for (int i = 0; i < ed->num_lines; i++) {
        line_free(&ed->lines[i]);
    }

    if (ed->lines != NULL) {
        kfree(ed->lines);
        ed->lines = NULL;
    }
}

/**
 * Main editor entry point
 */
void editor_run(const char *filename)
{
    editor_t ed;
    int key;

    if (editor_init(&ed, filename) < 0) {
        kprintf("Failed to initialize editor\n");
        return;
    }

    /* Clear screen */
    kprintf(ESC_CLEAR_SCREEN ESC_CURSOR_HOME);

    /* Main loop */
    while (!ed.should_quit) {
        editor_refresh_screen(&ed);
        key = editor_read_key();
        editor_process_key(&ed, key);
    }

    /* Clean up */
    editor_cleanup(&ed);

    /* Restore screen */
    kprintf(ESC_CLEAR_SCREEN ESC_CURSOR_HOME);
}

/* ============================================================================
 * End of editor.c
 * ============================================================================ */
