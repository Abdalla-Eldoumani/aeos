# Section 10: GUI Applications

## Overview

This section implements the built-in graphical applications for AEOS. These applications demonstrate how to build windowed programs using the window manager API, handle keyboard and mouse input, and integrate with system services like the filesystem and shell.

## Applications

### Terminal (terminal.c)
- **Location**: `src/apps/terminal.c`
- **Purpose**: GUI terminal emulator with shell access
- **Subsystem**: the shell (`src/kernel/shell.c`) plus `kprintf` output capture; the terminal hooks `kprintf` output into its cell buffer and runs commands through the shell
- **Features**:
  - 78x22 character text buffer using 8x16 cells (computed from window client area)
  - 16-color ANSI palette
  - ANSI CSI parser: `H`/`f` (cursor pos), `J`/`K` (clear), `m` (SGR: 0/7/30-37/40-47/90-97/100-107), `?25h`/`?25l` (cursor visibility)
  - Blinking cursor
  - 200-line scrollback ring (Page Up / Page Down to scroll, any other key snaps back to live)
  - Command input and execution; colorized shell prompt
  - Shell command integration via the `kprintf_output_hook`

### File Manager (filemanager.c)
- **Location**: `src/apps/filemanager.c`
- **Purpose**: Graphical file browser
- **Subsystem**: the VFS; lists directory contents via `vfs_open` / `vfs_readdir`, opens files via `vfs_read`, and surfaces open failures with `notify_error`
- **Features**:
  - Directory listing
  - File and folder icons
  - Path bar navigation
  - Keyboard navigation (up/down/enter)
  - Mouse click to select
  - Double-click to open directories or view files
  - File size display
  - **File viewer**: Opens regular files inline, shows content with scroll support (Up/Down arrows), Backspace/Escape to return to file list

### Settings (settings.c)
- **Location**: `src/apps/settings.c`
- **Purpose**: System information display
- **Subsystem**: the heap allocator statistics (`heap_get_stats` returns used/free) plus the uptime timer; read-only
- **Features**:
  - Memory usage (used/free/total)
  - System architecture info
  - CPU information
  - System uptime
  - Display resolution

### About (about.c)
- **Location**: `src/apps/about.c`
- **Purpose**: About dialog
- **Subsystem**: static system and architecture strings; no live kernel data source
- **Features**:
  - AEOS logo display
  - Version information
  - Architecture details
  - Platform information

### Calculator (calculator.c)
- **Location**: `src/apps/calculator.c`
- **Purpose**: Standard four-function calculator
- **Subsystem**: the int64 fixed-point arithmetic in `src/lib` (10^6 scale), chosen because `-mgeneral-regs-only` forbids floating point at EL1
- **Features**:
  - 200x260 window, 8x16 right-aligned display, 4x5 button grid (`C ± % ÷` row, then 7-9, 4-6, 1-3, then 0/./=)
  - Buttons rendered as `THEME_SURFACE_2` rects with `THEME_BORDER_SUBTLE`; operator labels use `THEME_ACCENT`
  - All arithmetic in int64 fixed-point at 10^6 scale, so decimals work under `-mgeneral-regs-only` (no FP)
  - `Error` shown on divide-by-zero; next digit press clears it

### System Monitor (sysmon.c)
- **Location**: `src/apps/sysmon.c`
- **Purpose**: Live heap usage graph
- **Subsystem**: the heap allocator statistics, sampled once per wall second into a 60-bar ring buffer; the paint hook calls `wm_request_redraw` each frame to keep the graph alive
- **Features**:
  - 280x180 window, header reads `Heap: USED / TOTAL`
  - 60-bar ring buffer, one bar per wall second, newest on the right
  - Sample driven inside `on_paint` from `timer_get_uptime_sec()`; the paint hook calls `wm_request_redraw()` so the WM keeps ticking even when nothing else changes

### Notes (notes.c)
- **Location**: `src/apps/notes.c`
- **Purpose**: Flat (non-modal) text editor
- **Subsystem**: the `editor_t` buffer engine (`src/kernel/editor.c`) plus the VFS; saves to `/notes.txt`
- **Features**:
  - 360x300 window, 8x16 cells, blinking caret driven from the WM clock
  - Wraps the `editor_t` storage from `src/kernel/editor.c` so the buffer + save logic stays in one place; `editor_init`, `editor_open`, `editor_save`, plus the now-public `editor_insert_char` / `editor_insert_newline` / `editor_delete_char` / `editor_backspace` helpers do all the buffer mutation
  - Saves to `/notes.txt` by default; Ctrl+S saves and posts a `notify_info("Saved")` toast; close also saves if dirty

### Tetris (tetris.c)
- **Location**: `src/apps/tetris.c`
- **Purpose**: Tetris game; exercises the framebuffer, the keyboard event path, the timer, and the VFS in one app
- **Subsystem**: the uptime timer for gravity pacing and the VFS for the high-score file `/tetris_high.bin`
- **Features**:
  - 10x20 board at 16 px per cell, all seven tetrominoes encoded as four rotations of a 16-bit grid each
  - Wall-clock gravity driven from inside `on_paint`: `timer_get_uptime_ms()` decides when the active piece falls, and `wm_request_redraw()` keeps the WM scheduler ticking when no input arrives
  - Standard Nintendo line-clear scoring (100/300/500/800 points scaled by level), level rises every 10 lines, drop interval shaves 50 ms per level down to a 100 ms floor
  - Arrow keys move/rotate, Down soft-drops, Space hard-drops with +2 per row, P pauses, R restarts on game over
  - High score persisted to `/tetris_high.bin` via the VFS: loaded on app create, saved on app close. The on-disk format is a 16-byte little-endian record (magic `AETT`, version, the `uint32_t` score, and a checksum); a truncated, wrong-magic, wrong-version, or bad-checksum file is treated as no saved score

## Application Architecture

All applications follow a common pattern:

```c
/* Application structure */
typedef struct {
    window_t *window;
    /* Application-specific state */
} myapp_t;

/* Create function */
myapp_t *myapp_create(void);

/* Destroy function */
void myapp_destroy(myapp_t *app);

/* Window callbacks */
static void myapp_paint(window_t *win);
static void myapp_key(window_t *win, key_event_t *key);
static void myapp_mouse(window_t *win, mouse_event_t *mouse);
static void myapp_close(window_t *win);
```

### Lifecycle

1. **Create**: Allocate state, create window, set callbacks, register with WM
2. **Paint**: Draw content to window's client area
3. **Input**: Handle keyboard and mouse events
4. **Close**: Unregister from WM, destroy window, free state

## API Reference

### Terminal

```c
/* Create terminal */
terminal_t *terminal_create(void);

/* Destroy terminal */
void terminal_destroy(terminal_t *term);

/* Write character to terminal */
void terminal_putchar(terminal_t *term, char c);

/* Write string to terminal */
void terminal_puts(terminal_t *term, const char *str);

/* Clear terminal */
void terminal_clear(terminal_t *term);

/* Set text colors */
void terminal_set_color(terminal_t *term, uint8_t fg, uint8_t bg);

/* Show shell prompt */
void terminal_show_prompt(terminal_t *term);

/* Execute shell command */
void terminal_execute_command(terminal_t *term, const char *cmd);

/* Handle key input */
void terminal_handle_key(terminal_t *term, key_event_t *key);

/* Get active terminal */
terminal_t *terminal_get_active(void);
```

### File Manager

```c
/* Create file manager */
filemanager_t *filemanager_create(void);

/* Destroy file manager */
void filemanager_destroy(filemanager_t *fm);

/* Refresh file listing */
void filemanager_refresh(filemanager_t *fm);

/* Navigate to directory */
void filemanager_navigate(filemanager_t *fm, const char *path);
```

### Settings

```c
/* Create settings window */
settings_t *settings_create(void);

/* Destroy settings window */
void settings_destroy(settings_t *settings);
```

### About

```c
/* Create about dialog */
about_t *about_create(void);

/* Destroy about dialog */
void about_destroy(about_t *about);
```

## Terminal Colors

| Index | Color | Hex |
|-------|-------|-----|
| 0 | Black | `0xFF000000` |
| 1 | Red | `0xFFCC0000` |
| 2 | Green | `0xFF00CC00` |
| 3 | Yellow | `0xFFCCCC00` |
| 4 | Blue | `0xFF0066CC` |
| 5 | Magenta | `0xFFCC00CC` |
| 6 | Cyan | `0xFF00CCCC` |
| 7 | White | `0xFFCCCCCC` |
| 8-15 | Bright variants | (brighter versions) |

## Window Callbacks

### on_paint

Called when window needs redrawing:
```c
void myapp_paint(window_t *win)
{
    myapp_t *app = (myapp_t *)win->user_data;

    /* Clear and draw content */
    window_clear(win, BG_COLOR);
    window_puts(win, 10, 10, "Hello", fg, bg);
}
```

### on_key

Called when key is pressed (in focused window):
```c
void myapp_key(window_t *win, key_event_t *key)
{
    myapp_t *app = (myapp_t *)win->user_data;

    if (key->ascii >= 32 && key->ascii < 127) {
        /* Printable character */
    }

    switch (key->keycode) {
        case KEY_ENTER: /* Handle enter */ break;
        case KEY_UP: /* Handle up arrow */ break;
    }

    window_invalidate(win);
}
```

### on_mouse

Called when mouse is clicked in window:
```c
void myapp_mouse(window_t *win, mouse_event_t *mouse)
{
    myapp_t *app = (myapp_t *)win->user_data;

    /* mouse->x, mouse->y are relative to client area */
    /* mouse->buttons indicates which buttons are pressed */

    window_invalidate(win);
}
```

### on_close

Called when window close button is clicked:
```c
void myapp_close(window_t *win)
{
    myapp_t *app = (myapp_t *)win->user_data;

    wm_unregister_window(win);
    window_destroy(win);
    kfree(app);
}
```

## Drawing Functions

All coordinates are relative to the window's client area (inside decorations).

```c
/* Clear entire client area */
window_clear(win, color);

/* Draw single pixel */
window_putpixel(win, x, y, color);

/* Draw filled rectangle */
window_fill_rect(win, x, y, width, height, color);

/* Draw rectangle outline */
window_draw_rect(win, x, y, width, height, color);

/* Draw line */
window_draw_line(win, x1, y1, x2, y2, color);

/* Draw character */
window_putchar(win, x, y, 'A', fg, bg);

/* Draw string */
window_puts(win, x, y, "Hello", fg, bg);
```

## Usage Examples

### Creating an Application

```c
myapp_t *myapp_create(void)
{
    myapp_t *app = kmalloc(sizeof(myapp_t));
    memset(app, 0, sizeof(myapp_t));

    /* Create window */
    app->window = window_create("My App", 100, 100, 400, 300,
                                 WINDOW_FLAG_VISIBLE);

    /* Set callbacks */
    app->window->on_paint = myapp_paint;
    app->window->on_key = myapp_key;
    app->window->on_close = myapp_close;
    app->window->user_data = app;

    /* Register with window manager */
    wm_register_window(app->window);

    return app;
}
```

### Adding to Desktop

In `gui.c`, add an icon and launch function:
```c
static void launch_myapp_icon(void)
{
    myapp_t *app = myapp_create();
    if (!app) {
        klog_error("Failed to launch My App");
    }
}

/* In gui_init() */
desktop_add_icon("My App", 0xFF00FF00, launch_myapp_icon);
```

## Known Limitations

### Terminal
- No command history navigation inside the GUI terminal
- No tab completion
- `edit` and `vi` are blocked from the GUI terminal because the editor's input loop reads UART directly and would freeze `wm_run`. The block message points users to text mode

### File Manager
- No file creation/deletion UI
- File content preview limited to 2KB
- Click-then-click for navigation (not true double-click)
- Maximum 64 entries per directory

### Settings
- Display only (no settings can be changed)
- No process list
- Static CPU information

### About
- Static content only

### Calculator
- 16-character display ceiling, so long results truncate
- No memory keys (M+, MR), no scientific functions

### System Monitor
- Heap is the only sampled metric (no CPU%, no per-process info, no userspace yet)
- Sampling is driven from the paint hook, so closing the window stops sampling

### Notes
- Single-file editor; cannot pick a different filename without changing `NOTES_DEFAULT_PATH`
- Plain text only; no syntax highlighting or selection