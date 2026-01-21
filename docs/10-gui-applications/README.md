# Section 10: GUI Applications

## Overview

This section implements the built-in graphical applications for AEOS. These applications demonstrate how to build windowed programs using the window manager API, handle keyboard and mouse input, and integrate with system services like the filesystem and shell.

## Applications

### Terminal (terminal.c)
- **Location**: `src/apps/terminal.c`
- **Purpose**: GUI terminal emulator with shell access
- **Features**:
  - 80x24 character text buffer
  - 16-color support (ANSI colors)
  - Blinking cursor
  - Command input and execution
  - Colorized shell prompt
  - Line scrolling
  - Shell command integration

### File Manager (filemanager.c)
- **Location**: `src/apps/filemanager.c`
- **Purpose**: Graphical file browser
- **Features**:
  - Directory listing
  - File and folder icons
  - Path bar navigation
  - Keyboard navigation (up/down/enter)
  - Mouse click to select
  - Double-click to open directories
  - File size display

### Settings (settings.c)
- **Location**: `src/apps/settings.c`
- **Purpose**: System information display
- **Features**:
  - Memory usage (used/free/total)
  - System architecture info
  - CPU information
  - System uptime
  - Display resolution

### About (about.c)
- **Location**: `src/apps/about.c`
- **Purpose**: About dialog
- **Features**:
  - AEOS logo display
  - Version information
  - Architecture details
  - Platform information

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
- No ANSI escape sequence parsing (colors set programmatically)
- No command history navigation
- No tab completion
- Fixed 80x24 size

### File Manager
- No file creation/deletion UI
- No file content preview
- Click-then-click for navigation (not true double-click)
- Maximum 64 entries per directory

### Settings
- Display only (no settings can be changed)
- No process list
- Static CPU information

### About
- Static content only