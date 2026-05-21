# GUI Applications - Implementation Details

> The snippets below are simplified illustrations. `include/aeos/apps/*.h`
> and `src/apps/*.c` are the source of truth; newer additions (ANSI parser
> state, scrollback ring, calculator/sysmon/notes/tetris) live there.

## Terminal Implementation

### Terminal Structure

```c
typedef struct {
    window_t *window;
    terminal_cell_t cells[TERMINAL_ROWS][TERMINAL_COLS];  /* 78x22 grid, 8x16 cells */
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint8_t current_fg;
    uint8_t current_bg;
    bool cursor_visible;
    bool cursor_blink_state;
    uint64_t last_blink;
    char input_buffer[256];
    uint32_t input_pos;
    bool input_ready;

    /* ANSI CSI parser state, scrollback ring; see header for full layout. */
    /* ... */
} terminal_t;

typedef struct {
    char ch;       /* Character */
    uint8_t fg;    /* Foreground color index */
    uint8_t bg;    /* Background color index */
} terminal_cell_t;
```

### Character Output

```c
void terminal_putchar(terminal_t *term, char c)
{
    if (c == '\n') {
        term->cursor_x = 0;
        term->cursor_y++;
    } else if (c == '\r') {
        term->cursor_x = 0;
    } else if (c == '\b') {
        if (term->cursor_x > 0) term->cursor_x--;
    } else if (c == '\t') {
        term->cursor_x = (term->cursor_x + 8) & ~7;  /* Align to 8 */
    } else if (c >= 32 && c < 127) {
        /* Store character in cell buffer */
        term->cells[term->cursor_y][term->cursor_x].ch = c;
        term->cells[term->cursor_y][term->cursor_x].fg = term->current_fg;
        term->cells[term->cursor_y][term->cursor_x].bg = term->current_bg;
        term->cursor_x++;
    }

    /* Line wrap */
    if (term->cursor_x >= TERMINAL_COLS) {
        term->cursor_x = 0;
        term->cursor_y++;
    }

    /* Scroll if needed */
    while (term->cursor_y >= TERMINAL_ROWS) {
        terminal_scroll(term);
        term->cursor_y--;
    }

    window_invalidate(term->window);
}
```

### Scrolling

```c
static void terminal_scroll(terminal_t *term)
{
    /* Move all lines up by one */
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
```

### Rendering

```c
static void terminal_paint(window_t *win)
{
    terminal_t *term = (terminal_t *)win->user_data;

    /* Clear background */
    window_clear(win, term_colors[TERM_COLOR_BLACK]);

    /* Draw each cell */
    for (row = 0; row < TERMINAL_ROWS; row++) {
        y = row * TERMINAL_CHAR_HEIGHT + 2;
        for (col = 0; col < TERMINAL_COLS; col++) {
            x = col * TERMINAL_CHAR_WIDTH + 4;

            fg = term_colors[term->cells[row][col].fg & 0x0F];
            bg = term_colors[term->cells[row][col].bg & 0x0F];

            window_putchar(win, x, y, term->cells[row][col].ch, fg, bg);
        }
    }

    /* Draw cursor (blinking) */
    if (term->cursor_visible && term->cursor_blink_state) {
        x = term->cursor_x * TERMINAL_CHAR_WIDTH + 4;
        y = term->cursor_y * TERMINAL_CHAR_HEIGHT + 2;

        /* White block cursor */
        window_fill_rect(win, x, y, TERMINAL_CHAR_WIDTH, TERMINAL_CHAR_HEIGHT,
                         term_colors[TERM_COLOR_WHITE]);

        /* Character in inverse video */
        if (term->cells[term->cursor_y][term->cursor_x].ch != ' ') {
            window_putchar(win, x, y,
                           term->cells[term->cursor_y][term->cursor_x].ch,
                           term_colors[TERM_COLOR_BLACK],
                           term_colors[TERM_COLOR_WHITE]);
        }
    }

    /* Update cursor blink state */
    uint64_t now = timer_get_uptime_ms();
    if (now - term->last_blink > 500) {
        term->cursor_blink_state = !term->cursor_blink_state;
        term->last_blink = now;
        window_invalidate(win);  /* Schedule another repaint */
    }
}
```

### Key Handling

```c
void terminal_handle_key(terminal_t *term, key_event_t *key)
{
    /* Printable characters */
    if (key->ascii >= 32 && key->ascii < 127) {
        if (term->input_pos < sizeof(term->input_buffer) - 1) {
            term->input_buffer[term->input_pos++] = key->ascii;
            terminal_putchar(term, key->ascii);  /* Echo */
        }
        return;
    }

    /* Special keys */
    switch (key->keycode) {
        case KEY_ENTER:
            terminal_putchar(term, '\n');
            term->input_buffer[term->input_pos] = '\0';

            /* Execute command */
            if (term->input_pos > 0) {
                terminal_execute_command(term, term->input_buffer);
            }

            /* Reset and show prompt */
            term->input_pos = 0;
            terminal_show_prompt(term);
            break;

        case KEY_BACKSPACE:
            if (term->input_pos > 0) {
                term->input_pos--;
                /* Erase character visually */
                if (term->cursor_x > 0) {
                    term->cursor_x--;
                    term->cells[term->cursor_y][term->cursor_x].ch = ' ';
                }
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
            break;
    }
}
```

### Command Execution

```c
void terminal_execute_command(terminal_t *term, const char *cmd)
{
    char line[SHELL_MAX_LINE];
    char *argv[SHELL_MAX_ARGS];
    int argc;

    /* Copy to mutable buffer */
    strncpy(line, cmd, SHELL_MAX_LINE - 1);

    /* Handle clear specially */
    if (strcmp(line, "clear") == 0) {
        terminal_clear(term);
        return;
    }

    /* Parse and execute via shell */
    if (shell_parse(line, &argc, argv) == 0 && argc > 0) {
        active_terminal = term;  /* For output redirection */
        shell_execute(argc, argv);
    }
}
```

## File Manager Implementation

### File Manager Structure

```c
typedef struct {
    char name[64];
    bool is_directory;
    uint32_t size;
} file_entry_t;

typedef struct {
    window_t *window;
    char current_path[256];
    file_entry_t entries[64];
    uint32_t entry_count;
    int32_t selected_index;
    uint32_t scroll_offset;
    uint32_t visible_entries;
    /* File viewer state */
    bool viewing_file;
    char view_filename[64];
    char view_content[2048];
    uint32_t view_content_len;
    uint32_t view_scroll;
} filemanager_t;
```

### Directory Listing

```c
void filemanager_refresh(filemanager_t *fm)
{
    int dir_fd;
    vfs_dirent_t dirent;
    uint32_t index = 0;

    fm->entry_count = 0;

    /* Add parent directory if not root */
    if (strcmp(fm->current_path, "/") != 0) {
        strcpy(fm->entries[index].name, "..");
        fm->entries[index].is_directory = true;
        index++;
    }

    /* Open directory */
    dir_fd = vfs_open(fm->current_path, O_RDONLY, 0);
    if (dir_fd < 0) return;

    /* Read entries */
    while (vfs_readdir(dir_fd, &dirent) == 0 && index < 64) {
        /* Skip . and .. from VFS */
        if (strcmp(dirent.name, ".") == 0 || strcmp(dirent.name, "..") == 0)
            continue;

        strncpy(fm->entries[index].name, dirent.name, 63);
        fm->entries[index].is_directory = (dirent.type == VFS_FILE_DIRECTORY);
        fm->entries[index].size = dirent.size;
        index++;
    }

    vfs_close(dir_fd);
    fm->entry_count = index;

    window_invalidate(fm->window);
}
```

### Navigation

```c
void filemanager_navigate(filemanager_t *fm, const char *path)
{
    int dir_fd = vfs_open(path, O_RDONLY, 0);
    if (dir_fd >= 0) {
        vfs_close(dir_fd);
        strncpy(fm->current_path, path, 255);
        fm->selected_index = -1;
        fm->scroll_offset = 0;
        filemanager_refresh(fm);
    }
}
```

### Rendering

```c
static void filemanager_paint(window_t *win)
{
    filemanager_t *fm = (filemanager_t *)win->user_data;
    int32_t y = FM_PATH_HEIGHT + 4;

    /* Clear and draw path bar */
    window_clear(win, FM_BG_COLOR);
    window_fill_rect(win, 0, 0, win->client_width, FM_PATH_HEIGHT, FM_PATH_BG);
    window_puts(win, FM_PADDING, 8, fm->current_path, 0xFFFFFFFF, FM_PATH_BG);

    /* Draw file entries */
    for (i = fm->scroll_offset; i < fm->entry_count && y < client_height; i++) {
        file_entry_t *entry = &fm->entries[i];

        /* Selection highlight */
        uint32_t bg = (i == fm->selected_index) ? FM_ENTRY_SELECTED : FM_ENTRY_BG;
        uint32_t text = entry->is_directory ? FM_FOLDER_COLOR : FM_FILE_COLOR;

        /* Background */
        window_fill_rect(win, FM_PADDING - 2, y, width, FM_ENTRY_HEIGHT - 2, bg);

        /* Icon */
        if (entry->is_directory) {
            draw_folder_icon(win, FM_PADDING, y + 2);
        } else {
            draw_file_icon(win, FM_PADDING, y + 1);
        }

        /* Name and size */
        window_puts(win, FM_PADDING + FM_ICON_WIDTH + 4, y + 6, entry->name, text, bg);

        y += FM_ENTRY_HEIGHT;
    }
}
```

### Mouse Handling

```c
static void filemanager_mouse(window_t *win, mouse_event_t *mouse)
{
    filemanager_t *fm = (filemanager_t *)win->user_data;

    /* Calculate clicked entry */
    if (mouse->y > FM_PATH_HEIGHT + 4) {
        int32_t idx = fm->scroll_offset + (mouse->y - FM_PATH_HEIGHT - 4) / FM_ENTRY_HEIGHT;

        if (idx >= 0 && idx < fm->entry_count) {
            if (fm->selected_index == idx) {
                /* Same entry clicked again - navigate or view */
                file_entry_t *entry = &fm->entries[idx];
                if (entry->is_directory) {
                    /* Build new path */
                    char new_path[256];
                    if (strcmp(entry->name, "..") == 0) {
                        /* Go to parent */
                        char *slash = strrchr(fm->current_path, '/');
                        if (slash && slash != fm->current_path) {
                            strncpy(new_path, fm->current_path, slash - fm->current_path);
                        } else {
                            strcpy(new_path, "/");
                        }
                    } else {
                        snprintf(new_path, sizeof(new_path), "%s/%s",
                                 fm->current_path, entry->name);
                    }
                    filemanager_navigate(fm, new_path);
                } else {
                    /* View file contents */
                    filemanager_view_file(fm, entry->name);
                }
            } else {
                /* Select entry */
                fm->selected_index = idx;
            }
        }
    }

    window_invalidate(win);
}
```

## Settings Implementation

### Structure

```c
typedef struct {
    window_t *window;
} settings_t;
```

### Rendering System Information

```c
static void settings_paint(window_t *win)
{
    heap_stats_t heap_stats;
    char buf[64];
    int32_t y = 10;

    window_clear(win, SETTINGS_BG);

    /* Title */
    window_puts(win, 10, y, "System Information", SETTINGS_HEADER, SETTINGS_BG);
    y += 24;

    /* Memory section */
    window_fill_rect(win, 10, y, win->client_width - 20, 80, SETTINGS_SECTION_BG);
    y += 8;

    window_puts(win, 20, y, "Memory", SETTINGS_HEADER, SETTINGS_SECTION_BG);
    y += 16;

    heap_get_stats(&heap_stats);

    window_puts(win, 20, y, "Used:", SETTINGS_LABEL, SETTINGS_SECTION_BG);
    format_size(buf, sizeof(buf), heap_stats.used_size);
    window_puts(win, 120, y, buf, SETTINGS_VALUE, SETTINGS_SECTION_BG);
    y += 14;

    window_puts(win, 20, y, "Free:", SETTINGS_LABEL, SETTINGS_SECTION_BG);
    format_size(buf, sizeof(buf), heap_stats.free_size);
    window_puts(win, 120, y, buf, SETTINGS_VALUE, SETTINGS_SECTION_BG);
    y += 14;

    /* System section */
    window_fill_rect(win, 10, y, win->client_width - 20, 80, SETTINGS_SECTION_BG);
    y += 8;

    window_puts(win, 20, y, "Arch:", SETTINGS_LABEL, SETTINGS_SECTION_BG);
    window_puts(win, 120, y, "ARMv8-A AArch64", SETTINGS_VALUE, SETTINGS_SECTION_BG);
    y += 14;

    window_puts(win, 20, y, "CPU:", SETTINGS_LABEL, SETTINGS_SECTION_BG);
    window_puts(win, 120, y, "Cortex-A57", SETTINGS_VALUE, SETTINGS_SECTION_BG);
    y += 14;

    /* Uptime */
    uint64_t uptime_sec = timer_get_uptime_sec();
    uint32_t hours = uptime_sec / 3600;
    uint32_t minutes = (uptime_sec % 3600) / 60;
    uint32_t seconds = uptime_sec % 60;

    snprintf(buf, sizeof(buf), "%u:%02u:%02u", hours, minutes, seconds);
    window_puts(win, 20, y, "Uptime:", SETTINGS_LABEL, SETTINGS_SECTION_BG);
    window_puts(win, 120, y, buf, SETTINGS_VALUE, SETTINGS_SECTION_BG);
}
```

### Format Size Helper

```c
static void format_size(char *buf, size_t bufsize, uint64_t bytes)
{
    if (bytes >= 1024 * 1024) {
        snprintf(buf, bufsize, "%u MB", (uint32_t)(bytes / (1024 * 1024)));
    } else if (bytes >= 1024) {
        snprintf(buf, bufsize, "%u KB", (uint32_t)(bytes / 1024));
    } else {
        snprintf(buf, bufsize, "%u B", (uint32_t)bytes);
    }
}
```

## About Implementation

### Logo Drawing

```c
static void draw_logo(window_t *win, int32_t x, int32_t y)
{
    /* Box background and border */
    window_fill_rect(win, x, y, 120, 60, 0xFF252540);
    window_draw_rect(win, x, y, 120, 60, ABOUT_LOGO_COLOR);
    window_draw_rect(win, x + 1, y + 1, 118, 58, ABOUT_LOGO_COLOR);

    /* "AEOS" text (doubled for bold effect) */
    window_puts(win, x + 20, y + 12, "A E O S", ABOUT_LOGO_COLOR, 0xFF252540);
    window_puts(win, x + 20, y + 22, "A E O S", ABOUT_LOGO_COLOR, 0xFF252540);

    /* Decorative line */
    window_fill_rect(win, x + 20, y + 38, 80, 2, ABOUT_LOGO_COLOR);

    /* Version */
    window_puts(win, x + 32, y + 44, "v1.0", ABOUT_TEXT, 0xFF252540);
}
```

### About Content

```c
static void about_paint(window_t *win)
{
    int32_t center_x = (win->client_width - 120) / 2;

    window_clear(win, ABOUT_BG);

    /* Centered logo */
    draw_logo(win, center_x, 10);

    y = 80;

    /* Title */
    window_puts(win, 30, y, "Abdalla's Educational", ABOUT_HIGHLIGHT, ABOUT_BG);
    y += 12;
    window_puts(win, 50, y, "Operating System", ABOUT_HIGHLIGHT, ABOUT_BG);
    y += 20;

    /* System info */
    window_puts(win, 50, y, "Architecture:", ABOUT_DIM, ABOUT_BG);
    window_puts(win, 150, y, "AArch64", ABOUT_TEXT, ABOUT_BG);
    y += 14;

    window_puts(win, 50, y, "Platform:", ABOUT_DIM, ABOUT_BG);
    window_puts(win, 150, y, "QEMU virt", ABOUT_TEXT, ABOUT_BG);
    y += 14;

    window_puts(win, 50, y, "Graphics:", ABOUT_DIM, ABOUT_BG);
    window_puts(win, 150, y, "VirtIO GPU", ABOUT_TEXT, ABOUT_BG);
    y += 20;

    /* Footer */
    window_puts(win, 60, y, "Built for learning!", ABOUT_LOGO_COLOR, ABOUT_BG);
}
```

## Common Patterns

### Window Creation

```c
app_t *app_create(void)
{
    app_t *app = kmalloc(sizeof(app_t));
    if (!app) return NULL;

    memset(app, 0, sizeof(app_t));

    /* Create window */
    app->window = window_create("Title", x, y, width, height, WINDOW_FLAG_VISIBLE);
    if (!app->window) {
        kfree(app);
        return NULL;
    }

    /* Set callbacks */
    app->window->on_paint = app_paint;
    app->window->on_key = app_key;
    app->window->on_mouse = app_mouse;
    app->window->on_close = app_close;
    app->window->user_data = app;

    /* Register */
    wm_register_window(app->window);

    return app;
}
```

### Close Handler

Window close is **deferred** in the WM: clicking the close button stamps `close_anim_start_ms` and the WM reaps the window after `WINDOW_CLOSE_ANIM_MS`. The reap path is what calls `on_close`. To avoid an in-flight event landing on a freed callback, null the callbacks **before** unregistering the window:

```c
static void app_close(window_t *win)
{
    app_t *app = (app_t *)win->user_data;

    /* Null callbacks first so any pending event sees NULL, not a stale fn. */
    win->on_paint = NULL;
    win->on_key   = NULL;
    win->on_mouse = NULL;
    win->on_close = NULL;

    wm_unregister_window(win);
    window_destroy(win);

    if (app) {
        kfree(app);
    }
}
```

### Client-Area Drawing

Apps draw inside the client area only and use the `window_*` helpers
(`window_clear`, `window_fill_rect`, `window_putchar`, `window_puts`, and the
rest), never the raw `fb_*` primitives. The `window_*` helpers translate
client-relative coordinates into framebuffer coordinates and keep the window
decorations untouched, so an app cannot paint over its own title bar or a
neighbor's pixels.

### Editor Apps in the GUI

`edit` and `vi` are blocked from the GUI Terminal because the editor's input
loop calls `uart_getc` directly and would block `wm_run`, freezing the whole
desktop. Editing happens in text mode instead. The Terminal's output rendering
is ANSI-parser-compatible, so command output still displays correctly. The
Notes app is the GUI editor: it wraps the same `editor_t` buffer engine but
drives input through the window callbacks, so it never blocks the WM loop.

## Tetris Implementation

### Tetris Structure

```c
typedef struct {
    window_t *window;
    uint8_t board[TET_ROWS][TET_COLS];  /* 20 rows x 10 cols */
    int piece_type, piece_rot;
    int piece_x, piece_y;
    uint32_t score, high_score, lines_cleared, level;
    uint64_t last_drop_ms;              /* gravity clock */
    bool paused, game_over;
} tetris_t;
```

The board is 10 columns by 20 rows at 16 px per cell. All seven tetrominoes are
encoded as four rotations of a 16-bit grid each, in a static table.

### Gravity from the Paint Hook

```c
static void tetris_paint(window_t *win)
{
    tetris_t *t = (tetris_t *)win->user_data;
    uint64_t now = timer_get_uptime_ms();

    /* Drop the active piece when the per-level interval elapses. */
    if (!t->paused && !t->game_over && now - t->last_drop_ms >= tet_drop_ms(t)) {
        tet_drop_one(t);
        t->last_drop_ms = now;
    }

    /* draw board, active piece, score panel ... */

    wm_request_redraw();  /* keep the WM ticking with no input */
}
```

Gravity is paced by `timer_get_uptime_ms`: the active piece falls when the
per-level interval has elapsed, and `wm_request_redraw` keeps the compositor
ticking even when no key is pressed. The drop interval shaves 50 ms per level
down to a 100 ms floor; the level rises every 10 cleared lines. Line clears use
the Nintendo scoring table (100/300/500/800 points scaled by level). Arrow keys
move and rotate, Down soft-drops, Space hard-drops, P pauses, R restarts.

### High-Score Persistence

```c
#define TET_HIGH_SCORE_PATH "/tetris_high.bin"

static uint32_t tet_load_high_score(void)
{
    int fd = vfs_open(TET_HIGH_SCORE_PATH, O_RDONLY, 0);
    if (fd < 0) return 0;

    tet_high_record_t rec;
    ssize_t n = vfs_read(fd, &rec, sizeof(rec));
    vfs_close(fd);

    /* Short read, wrong magic/version, or bad checksum means no saved score. */
    if (n != (ssize_t)sizeof(rec) ||
        rec.magic != TET_HIGH_MAGIC || rec.version != TET_HIGH_VERSION ||
        (rec.magic ^ rec.version ^ rec.score) != rec.checksum) {
        return 0;
    }
    return rec.score;
}
```

The high score lives in `/tetris_high.bin` through the VFS, loaded on
`tetris_create` and written on `tetris_close` with `vfs_open` / `vfs_write`.
The on-disk record is 16 bytes: a magic (`AETT`), a version, the `uint32_t`
score, and a checksum. A truncated or tampered file is read back as no saved
score, so a corrupt file never puts garbage on the side panel.

## Debugging

### Trace Key Input
```c
klog_debug("Key: keycode=%u ascii=%c mods=0x%x",
           key->keycode, key->ascii, key->modifiers);
```

### Trace Mouse Input
```c
klog_debug("Mouse: x=%d y=%d buttons=0x%x",
           mouse->x, mouse->y, mouse->buttons);
```

### Trace Paint Calls
```c
klog_debug("Paint: %s (%dx%d)",
           win->title, win->client_width, win->client_height);
```