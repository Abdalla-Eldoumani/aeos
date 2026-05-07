/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/apps/notes.c
 * Description: GUI notes editor. Reuses the editor.c buffer engine
 *              (editor_t plus the insert/delete/backspace helpers) so the
 *              storage and save logic stay in one place. The vim-style modal
 *              key handling is intentionally NOT wired up — Notes is a flat
 *              "type to insert, backspace to delete, Ctrl+S to save" app.
 * ============================================================================ */

#include <aeos/apps/notes.h>
#include <aeos/window.h>
#include <aeos/wm.h>
#include <aeos/heap.h>
#include <aeos/string.h>
#include <aeos/event.h>
#include <aeos/timer.h>
#include <aeos/notify.h>
#include <aeos/kprintf.h>
#include <aeos/theme.h>

#define NOTES_PAD_X         6
#define NOTES_PAD_Y         6
#define NOTES_CELL_W        8
#define NOTES_CELL_H       16
#define NOTES_CARET_BLINK  500u  /* ms */

static void notes_paint(window_t *win);
static void notes_key(window_t *win, key_event_t *key);
static void notes_close(window_t *win);

/**
 * How many cell rows / cols fit in our 360x300 window after padding.
 * Computed once and reused — the window can't be resized at runtime, so
 * caching keeps the paint loop simple.
 */
static int notes_cols(void)
{
    int client_w = NOTES_WIN_WIDTH - 2 * (int)WINDOW_BORDER_WIDTH;
    return (client_w - 2 * NOTES_PAD_X) / NOTES_CELL_W;
}

static int notes_rows(void)
{
    int client_h = NOTES_WIN_HEIGHT - (int)WINDOW_TITLE_HEIGHT - (int)WINDOW_BORDER_WIDTH;
    return (client_h - 2 * NOTES_PAD_Y) / NOTES_CELL_H;
}

/**
 * Adjust the editor's scroll offsets so the cursor stays in view. Called
 * after every move that might push the cursor off-screen.
 */
static void scroll_to_cursor(notes_t *n)
{
    int rows = notes_rows();
    int cols = notes_cols();
    if (n->ed.cursor_row < n->ed.scroll_row) {
        n->ed.scroll_row = n->ed.cursor_row;
    } else if (n->ed.cursor_row >= n->ed.scroll_row + rows) {
        n->ed.scroll_row = n->ed.cursor_row - rows + 1;
    }
    if (n->ed.cursor_col < n->ed.scroll_col) {
        n->ed.scroll_col = n->ed.cursor_col;
    } else if (n->ed.cursor_col >= n->ed.scroll_col + cols) {
        n->ed.scroll_col = n->ed.cursor_col - cols + 1;
    }
}

/* Clamp cursor_col so it never sits past the end of the line it's on. */
static void clamp_cursor(notes_t *n)
{
    int line_len;
    if (n->ed.cursor_row < 0) n->ed.cursor_row = 0;
    if (n->ed.cursor_row >= n->ed.num_lines) {
        n->ed.cursor_row = n->ed.num_lines - 1;
    }
    line_len = (int)n->ed.lines[n->ed.cursor_row].len;
    if (n->ed.cursor_col > line_len) n->ed.cursor_col = line_len;
    if (n->ed.cursor_col < 0) n->ed.cursor_col = 0;
}

static void notes_paint(window_t *win)
{
    notes_t *n = (notes_t *)win->user_data;
    int      rows, cols;
    int      r, screen_row, screen_col, c;
    editor_line_t *line;
    int32_t  px, py;
    bool     caret_on;
    uint64_t now_ms;

    if (!n) return;

    rows = notes_rows();
    cols = notes_cols();

    window_clear(win, THEME_SURFACE_1);

    /* Render visible lines. */
    for (r = 0; r < rows; r++) {
        screen_row = n->ed.scroll_row + r;
        if (screen_row >= n->ed.num_lines) break;
        line = &n->ed.lines[screen_row];
        py = NOTES_PAD_Y + r * NOTES_CELL_H;

        for (screen_col = 0; screen_col < cols; screen_col++) {
            c = n->ed.scroll_col + screen_col;
            if ((size_t)c >= line->len) break;
            px = NOTES_PAD_X + screen_col * NOTES_CELL_W;
            window_putchar_large(win, px, py, line->chars[c],
                                 THEME_TEXT_PRIMARY, THEME_SURFACE_1);
        }
    }

    /* Caret — half-second blink driven off the wall clock. */
    now_ms = timer_get_uptime_ms();
    caret_on = ((now_ms / NOTES_CARET_BLINK) & 1u) == 0;
    if (caret_on) {
        screen_row = n->ed.cursor_row - n->ed.scroll_row;
        screen_col = n->ed.cursor_col - n->ed.scroll_col;
        if (screen_row >= 0 && screen_row < rows &&
            screen_col >= 0 && screen_col < cols) {
            px = NOTES_PAD_X + screen_col * NOTES_CELL_W;
            py = NOTES_PAD_Y + screen_row * NOTES_CELL_H;
            window_fill_rect(win, px, py + NOTES_CELL_H - 2,
                             NOTES_CELL_W, 2,
                             THEME_ACCENT);
        }
    }

    /* Drive the next frame so the caret blink stays alive. */
    wm_request_redraw();
}

static void notes_key(window_t *win, key_event_t *key)
{
    notes_t *n = (notes_t *)win->user_data;

    if (!n) return;

    /* Ctrl+S saves through the editor engine and posts a confirmation toast. */
    if ((key->modifiers & MOD_CTRL) && key->keycode == KEY_S) {
        if (editor_save(&n->ed) == 0) {
            notify_info("Saved");
        } else {
            notify_error("Save failed");
        }
        wm_request_redraw();
        return;
    }

    switch (key->keycode) {
    case KEY_ENTER:
        editor_insert_newline(&n->ed);
        scroll_to_cursor(n);
        break;
    case KEY_BACKSPACE:
        editor_backspace(&n->ed);
        scroll_to_cursor(n);
        break;
    case KEY_DELETE:
        editor_delete_char(&n->ed);
        break;
    case KEY_LEFT:
        if (n->ed.cursor_col > 0) {
            n->ed.cursor_col--;
        } else if (n->ed.cursor_row > 0) {
            n->ed.cursor_row--;
            n->ed.cursor_col = (int)n->ed.lines[n->ed.cursor_row].len;
        }
        scroll_to_cursor(n);
        break;
    case KEY_RIGHT:
        if (n->ed.cursor_col < (int)n->ed.lines[n->ed.cursor_row].len) {
            n->ed.cursor_col++;
        } else if (n->ed.cursor_row + 1 < n->ed.num_lines) {
            n->ed.cursor_row++;
            n->ed.cursor_col = 0;
        }
        scroll_to_cursor(n);
        break;
    case KEY_UP:
        if (n->ed.cursor_row > 0) {
            n->ed.cursor_row--;
            clamp_cursor(n);
            scroll_to_cursor(n);
        }
        break;
    case KEY_DOWN:
        if (n->ed.cursor_row + 1 < n->ed.num_lines) {
            n->ed.cursor_row++;
            clamp_cursor(n);
            scroll_to_cursor(n);
        }
        break;
    case KEY_HOME:
        n->ed.cursor_col = 0;
        scroll_to_cursor(n);
        break;
    case KEY_END:
        n->ed.cursor_col = (int)n->ed.lines[n->ed.cursor_row].len;
        scroll_to_cursor(n);
        break;
    default:
        if (key->ascii >= 32 && key->ascii < 127) {
            editor_insert_char(&n->ed, key->ascii);
            scroll_to_cursor(n);
        } else if (key->ascii == '\t') {
            /* Soft tab — 4 spaces. The editor allocates as needed. */
            int i;
            for (i = 0; i < 4; i++) editor_insert_char(&n->ed, ' ');
            scroll_to_cursor(n);
        }
        break;
    }
    wm_request_redraw();
}

static void notes_close(window_t *win)
{
    notes_t *n = (notes_t *)win->user_data;

    win->on_paint = NULL;
    win->on_key   = NULL;
    win->on_mouse = NULL;
    win->on_close = NULL;

    wm_unregister_window(win);
    window_destroy(win);

    if (n) {
        if (n->ed_initialized && n->ed.modified) {
            /* Best-effort save; the toast ack happens via Ctrl+S, this is
             * just the don't-lose-data backstop. */
            editor_save(&n->ed);
        }
        if (n->ed_initialized) {
            editor_cleanup(&n->ed);
        }
        kfree(n);
    }
}

notes_t *notes_create(void)
{
    notes_t *n;

    n = (notes_t *)kmalloc(sizeof(notes_t));
    if (!n) {
        klog_error("Failed to allocate notes app");
        return NULL;
    }
    memset(n, 0, sizeof(*n));

    if (editor_init(&n->ed, NOTES_DEFAULT_PATH) != 0) {
        klog_error("notes: editor_init failed");
        kfree(n);
        return NULL;
    }
    n->ed_initialized = true;

    n->window = window_create("Notes", 160, 80,
                              NOTES_WIN_WIDTH, NOTES_WIN_HEIGHT,
                              WINDOW_FLAG_VISIBLE);
    if (!n->window) {
        editor_cleanup(&n->ed);
        kfree(n);
        return NULL;
    }
    n->window->on_paint = notes_paint;
    n->window->on_key   = notes_key;
    n->window->on_close = notes_close;
    n->window->user_data = n;

    wm_register_window(n->window);
    return n;
}

void notes_destroy(notes_t *n)
{
    if (!n) return;
    kfree(n);
}

/* ============================================================================
 * End of notes.c
 * ============================================================================ */
