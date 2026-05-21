# Section 08: Graphics and GUI

## Overview

This section implements the complete graphical desktop environment for AEOS. It includes an animated boot screen, event-driven input handling, a compositing window manager, and a desktop environment with icons and taskbar. The system transforms AEOS from a text-only shell into a visual operating system with mouse support and draggable windows.

## Components

### Boot Screen (bootscreen.c)
- **Location**: `src/kernel/bootscreen.c`
- **Purpose**: Visual boot progress display
- **Features**:
  - 8x16 wordmark `A E O S` over an 8x8 "Educational Operating System" subtitle
  - 4-px slim progress bar at 60% width (no percentage number)
  - Stage messages cross-fade over 240 ms with cubic ease-out
  - 200 ms fade-to-`THEME_BG_DEEP` before the desktop appears
  - Text mode fallback (press 'T' during boot)
  - Footer: text-mode hint above `AArch64 | cortex-a57 | 256 MB` hardware line
  - Animations driven from `timer_get_uptime_ms` which reads `CNTVCT_EL0` directly so the timing pages with wall time on QEMU

### Event System (event.c)
- **Location**: `src/kernel/event.c`
- **Purpose**: Unified input event handling
- **Features**:
  - Circular event queue (64 events)
  - Mouse events (move, button up/down)
  - Keyboard events (key down/up with modifiers)
  - Keycode to ASCII conversion
  - UART keyboard fallback

### Window Manager (wm.c)
- **Location**: `src/kernel/wm.c`
- **Purpose**: Window compositing and management
- **Features**:
  - Z-ordered window list (front to back); doubly linked
  - Focus tracking and switching (`wm_focus_window` raises; `focus_no_raise` for Alt+Tab cycling)
  - Window dragging by title bar with edge clamping (32 px of title bar stays on-screen)
  - Window open animation (slide-up + fade), deferred close animation, focused-window drop shadow
  - Mouse cursor rendering with backup/restore
  - 30 FPS display refresh; toasts force per-frame redraws while alive
  - Keyboard shortcuts: Alt+Tab focus cycle, Alt+F4 close-via-fade, Esc dismiss start menu + toasts
  - `wm_request_redraw()` for live-content apps that need the WM to keep ticking

### Window manager behavior

The behavior below is the current compositor. The snippet sections under
`docs/08-graphics-gui/implementation.md` and the in-tree `src/kernel/CLAUDE.md`
carry the function-level detail.

- **Redraw trigger.** `wm_update_display` repaints when `wm.needs_redraw` is true
  OR `notify_active()` is true. `window_invalidate(win)` sets that window's
  `WINDOW_FLAG_DIRTY` AND calls `wm_request_redraw()` so the next compositor tick
  takes the redraw branch instead of skipping it. Apps call `window_invalidate`
  from their `on_mouse` / `on_key` handlers after mutating state. Live-content
  apps that need a frame without an input event (the System Monitor heap graph,
  the Notes caret blink) call `wm_request_redraw()` directly each tick.
- **Open animation.** `wm_register_window` stamps `open_anim_start_ms`.
  `window_draw` slides the draw position down by `WINDOW_OPEN_SLIDE_PX` and eases
  it up over `WINDOW_OPEN_ANIM_MS`, blending a `THEME_BG_DEEP` overlay at
  decreasing alpha. Hit-testing keeps the unanimated coordinates, so clicks land
  where the user sees the final window position.
- **Deferred close.** Clicking the close button or pressing Alt+F4 sets
  `WINDOW_FLAG_CLOSING` and stamps `close_anim_start_ms`. `wm_run` runs the
  fade-out, then `reap_closing_windows` destroys the window only after
  `WINDOW_CLOSE_ANIM_MS` has elapsed, and only then invokes `on_close`. Events
  on a closing window are ignored so the fade cannot be interrupted.
- **Focused-window drop shadow.** The focused window currently gets the
  design-system fallback shadow: a single 2-px `THEME_BORDER_SUBTLE` line offset
  1 px below the window. The richer three-rect alpha shadow is staged behind
  `fb_blend_rect` but is not the active path.

### Global keyboard shortcuts

Handled in `wm.c::handle_key`:

- **Alt+Tab** cycles focus through the visible windows in z-order without
  raising (`focus_no_raise` / `next_focus_candidate`); the cycled window is
  raised when Alt is released.
- **Alt+F4** routes the focused window through the same close-button fade-out
  rather than destroying it immediately.
- **Esc** dismisses the start menu and force-fades active toasts
  (`notify_dismiss_all`) before falling through to the focused window.

### Window (window.c)
- **Location**: `src/kernel/window.c`
- **Purpose**: Individual window handling
- **Features**:
  - Window creation and destruction
  - Window decorations (title bar, close button)
  - Client area management
  - Drawing primitives (putpixel, fill_rect, puts)
  - Hit testing for title bar and close button

### Desktop (desktop.c)
- **Location**: `src/kernel/desktop.c`
- **Purpose**: Desktop environment
- **Features**:
  - Gradient background
  - Desktop icons with selection
  - Double-click to launch applications
  - Taskbar with Start button
  - Window buttons in taskbar
  - System clock (uptime-based)
  - Start menu popup

### GUI Integration (gui.c)
- **Location**: `src/kernel/gui.c`
- **Purpose**: Coordinate GUI subsystem initialization
- **Features**:
  - Initialize all GUI components in order
  - Register seven desktop icons (Terminal, Files, Settings, About, Calc, SysMon, Notes) with launch callbacks
  - Provide application launch functions (`gui_launch_*`)
  - Main GUI entry point

### Notifications (notify.c)
- **Location**: `src/kernel/notify.c`
- **Purpose**: Floating toast notifications above windows
- **Features**:
  - Three-slot ring; oldest evicted on overflow
  - 220 ms slide-in from the right, 4 s visible, 240 ms fade-out (eased)
  - Per-level stripe color: `THEME_ACCENT` (info), `THEME_WARNING` (warn), `THEME_DANGER` (error)
  - Composited above windows but below the cursor in `wm_update_display`
  - `notify_dismiss_all()` for the Esc shortcut force-fades everything at once

## Architecture

### Event Flow

```
VirtIO Input Devices     UART
        |                  |
        v                  v
   virtio_input_poll()  event_poll()
        |                  |
        +--------+---------+
                 |
                 v
        Event Queue (circular buffer)
                 |
                 v
           wm_handle_event()
                 |
     +-----------+-----------+
     |           |           |
     v           v           v
  Mouse       Mouse      Keyboard
  Move        Button       Key
     |           |           |
     v           v           v
  Cursor     Desktop/    Focused
  Update     Window      Window
             Click       Callback
```

### Window Hierarchy

```
Window Manager (wm.c)
    |
    +-- Window List (linked list, bottom to top)
    |       |
    |       +-- window_t (prev, next pointers)
    |       +-- window_t
    |       +-- window_t (top_window, focused)
    |
    +-- Desktop Paint Callback
            |
            +-- desktop_paint()
                    |
                    +-- Background
                    +-- Icons
                    +-- Taskbar
                    +-- Start Menu
```

### Rendering Pipeline

```
1. wm_update_display()
   |
   +-- restore_cursor_background()
   |
   +-- if (needs_redraw):
   |       wm_redraw()
   |           |
   |           +-- desktop_paint()     # Background, icons, taskbar
   |           +-- for each window:    # Bottom to top
   |                   window_draw()
   |                       |
   |                       +-- window_draw_decorations()
   |                       +-- fb_fill_rect() (client area)
   |                       +-- win->on_paint() callback
   |
   +-- save_cursor_background()
   |
   +-- wm_draw_cursor()
   |
   +-- virtio_gpu_update_display()     # Send to GPU
```

## Color Scheme

### Boot Screen
| Element | Color | Hex |
|---------|-------|-----|
| Background | Dark blue-gray | `0xFF1A1A2E` |
| Logo | Cyan | `0xFF00D9FF` |
| Progress bar fill | Cyan | `0xFF00D9FF` |
| Text | Light gray | `0xFFE0E0E0` |

### Desktop
| Element | Color | Hex |
|---------|-------|-----|
| Background top | Dark blue | `0xFF1a1a2e` |
| Background bottom | Darker blue | `0xFF16213e` |
| Taskbar | Very dark | `0xFF0f0f23` |
| Start button | Green | `0xFF00aa55` |
| Clock | Light gray | `0xFFcccccc` |

### Windows
| Element | Color | Hex |
|---------|-------|-----|
| Title bar (focused) | Blue | `0xFF2060A0` |
| Title bar (unfocused) | Dark gray | `0xFF404040` |
| Close button | Red | `0xFFCC4444` |
| Client area | Dark | `0xFF202020` |
| Border | Gray | `0xFF505050` |

## API Reference

### Boot Screen

```c
/* Initialize and display boot screen */
void bootscreen_init(void);

/* Update progress to specified stage */
void bootscreen_update(boot_stage_t stage);

/* Set custom progress message and percentage */
void bootscreen_set_progress(const char *message, uint32_t progress);

/* Complete boot screen, returns true for GUI mode */
bool bootscreen_complete(void);

/* Check if user requested text mode */
bool bootscreen_text_mode_requested(void);
```

### Event System

```c
/* Initialize event system */
void event_init(void);

/* Push event to queue */
bool event_push(const event_t *event);

/* Pop event from queue */
bool event_pop(event_t *event);

/* Poll all input devices */
void event_poll(void);

/* Generate events from input drivers */
void event_generate_key(keycode_t keycode, bool pressed);
void event_generate_mouse_move(int32_t dx, int32_t dy);
void event_generate_mouse_button(uint8_t button, bool pressed);

/* Get mouse position */
void event_get_mouse_pos(int32_t *x, int32_t *y);
```

### Window Manager

```c
/* Initialize window manager */
void wm_init(void);

/* Register/unregister windows */
void wm_register_window(window_t *win);
void wm_unregister_window(window_t *win);

/* Focus management */
void wm_focus_window(window_t *win);
window_t *wm_get_focused_window(void);

/* Find window at position */
window_t *wm_window_at(int32_t x, int32_t y);

/* Main loop */
void wm_run(void);
void wm_request_exit(void);

/* Set desktop paint callback */
void wm_set_desktop_paint(wm_desktop_paint_fn fn);
```

### Window

```c
/* Create/destroy windows */
window_t *window_create(const char *title, int32_t x, int32_t y,
                        uint32_t width, uint32_t height, uint32_t flags);
void window_destroy(window_t *win);

/* Visibility */
void window_show(window_t *win);
void window_hide(window_t *win);

/* Properties */
void window_set_title(window_t *win, const char *title);
void window_move(window_t *win, int32_t x, int32_t y);
void window_resize(window_t *win, uint32_t width, uint32_t height);

/* Drawing in client area */
void window_clear(window_t *win, uint32_t color);
void window_putpixel(window_t *win, int32_t x, int32_t y, uint32_t color);
void window_fill_rect(window_t *win, int32_t x, int32_t y,
                      uint32_t w, uint32_t h, uint32_t color);
void window_puts(window_t *win, int32_t x, int32_t y,
                 const char *text, uint32_t fg, uint32_t bg);
```

### Desktop

```c
/* Initialize desktop */
void desktop_init(void);

/* Add desktop icon */
int desktop_add_icon(const char *name, uint32_t icon_color,
                     void (*on_launch)(void));

/* Handle input */
void desktop_handle_click(int32_t x, int32_t y, bool double_click);
void desktop_handle_taskbar_click(int32_t x, int32_t y);
bool desktop_is_taskbar_click(int32_t y);

/* Drawing */
void desktop_paint(void);
```

### GUI Integration

```c
/* Initialize GUI subsystem */
int gui_init(void);

/* Run GUI main loop */
void gui_run(void);

/* Launch applications */
void gui_launch_terminal(void);
void gui_launch_filemanager(void);
void gui_launch_settings(void);
void gui_launch_about(void);
void gui_launch_calculator(void);
void gui_launch_sysmon(void);
void gui_launch_notes(void);
```

## Boot Stages

| Stage | Progress | Message |
|-------|----------|---------|
| `BOOT_STAGE_MEMORY` | 10% | "Initializing memory..." |
| `BOOT_STAGE_INTERRUPTS` | 20% | "Setting up interrupts..." |
| `BOOT_STAGE_TIMER` | 30% | "Starting timer..." |
| `BOOT_STAGE_FILESYSTEM` | 50% | "Loading file system..." |
| `BOOT_STAGE_PROCESSES` | 70% | "Initializing processes..." |
| `BOOT_STAGE_INPUT` | 85% | "Starting input devices..." |
| `BOOT_STAGE_DESKTOP` | 100% | "Loading desktop..." |

## Event Types

| Type | Description |
|------|-------------|
| `EVENT_KEY_DOWN` | Key pressed |
| `EVENT_KEY_UP` | Key released |
| `EVENT_MOUSE_MOVE` | Mouse position changed |
| `EVENT_MOUSE_BUTTON_DOWN` | Mouse button pressed |
| `EVENT_MOUSE_BUTTON_UP` | Mouse button released |

## Window Flags

| Flag | Description |
|------|-------------|
| `WINDOW_FLAG_VISIBLE` | Window is visible |
| `WINDOW_FLAG_FOCUSED` | Window has focus |
| `WINDOW_FLAG_DECORATED` | Draw title bar and border |
| `WINDOW_FLAG_DIRTY` | Needs redraw |
| `WINDOW_FLAG_DRAGGING` | Being dragged |

## Usage

### Enabling Graphical Mode

```bash
make run-ramfb     # Boot into graphical desktop
```

### Text Mode Fallback

During boot, press 'T' to skip GUI and enter text shell.

### Desktop Interaction

- **Single-click** icon: Select (highlight)
- **Double-click** icon: Launch application
- **Drag** title bar: Move window
- **Click** close button: Close window
- **Click** taskbar button: Focus window
- **Click** Start button: Toggle start menu

## Memory Layout

| Component | Approximate Size |
|-----------|-----------------|
| Main framebuffer | 1.2 MB (640x480x4) |
| Event queue | ~4 KB |
| Window structures | ~1 KB each |
| Cursor backup | ~960 bytes |
| Desktop icons | ~2 KB |

## Known Limitations

- No window resizing (fixed size at creation)
- No window minimization
- Double-click timing fixed at 500ms
- Maximum 16 desktop icons
