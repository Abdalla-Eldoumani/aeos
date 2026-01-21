# Graphics and GUI - Implementation Details

## Boot Screen Implementation

### Boot Stages

```c
static const boot_stage_info_t boot_stages[] = {
    { "Initializing memory...",      10 },
    { "Setting up interrupts...",    20 },
    { "Starting timer...",           30 },
    { "Loading file system...",      50 },
    { "Initializing processes...",   70 },
    { "Starting input devices...",   85 },
    { "Loading desktop...",          100 },
    { "Boot complete!",              100 }
};
```

Boot stages are predefined with messages and progress percentages. The kernel calls `bootscreen_update()` after each initialization step.

### Progress Bar Rendering

```c
static void draw_progress_bar(uint32_t progress)
{
    uint32_t bar_x = (SCREEN_WIDTH - PROGRESS_BAR_WIDTH) / 2;
    uint32_t bar_y = PROGRESS_BAR_Y;
    uint32_t fill_width;

    /* Clamp progress */
    if (progress > 100) progress = 100;

    /* Draw background */
    draw_rounded_rect(bar_x, bar_y, PROGRESS_BAR_WIDTH, PROGRESS_BAR_HEIGHT,
                      BOOT_PROGRESS_BG);

    /* Draw fill - width proportional to progress */
    fill_width = (PROGRESS_BAR_WIDTH - 4) * progress / 100;
    if (fill_width > 0) {
        fb_fill_rect(bar_x + 2, bar_y + 2,
                     fill_width, PROGRESS_BAR_HEIGHT - 4,
                     BOOT_PROGRESS_FG);
    }

    /* Draw border */
    fb_draw_rect(bar_x, bar_y, PROGRESS_BAR_WIDTH, PROGRESS_BAR_HEIGHT,
                 BOOT_BORDER_COLOR);
}
```

The progress bar is centered horizontally. Fill width is calculated as a percentage of the total bar width.

### Text Mode Detection

```c
static void check_text_mode_key(void)
{
    if (uart_data_available()) {
        char c = uart_getc();
        if (c == 'T' || c == 't') {
            text_mode_requested = true;
            klog_info("Text mode requested by user");
        }
    }
}
```

During boot, the system polls UART for the 'T' key. If pressed, `bootscreen_complete()` returns `false` and the kernel skips GUI initialization.

## Event System Implementation

### Event Queue

```c
#define EVENT_QUEUE_SIZE 64

static event_t event_queue[EVENT_QUEUE_SIZE];
static uint32_t queue_head = 0;
static uint32_t queue_tail = 0;
static uint32_t queue_count = 0;
```

The event queue is a circular buffer. `queue_head` points to the next event to pop, `queue_tail` points to where the next event will be pushed.

### Push and Pop

```c
bool event_push(const event_t *event)
{
    if (queue_count >= EVENT_QUEUE_SIZE) {
        return false;  /* Queue full */
    }

    event_queue[queue_tail] = *event;
    queue_tail = (queue_tail + 1) % EVENT_QUEUE_SIZE;
    queue_count++;
    return true;
}

bool event_pop(event_t *event)
{
    if (queue_count == 0) {
        return false;  /* Queue empty */
    }

    *event = event_queue[queue_head];
    queue_head = (queue_head + 1) % EVENT_QUEUE_SIZE;
    queue_count--;
    return true;
}
```

Both operations are O(1). The modulo operation handles wraparound.

### Mouse Event Generation

```c
void event_generate_mouse_move(int32_t dx, int32_t dy)
{
    event_t event;

    /* Update mouse position */
    mouse_x += dx;
    mouse_y += dy;

    /* Clamp to screen bounds */
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= FB_WIDTH) mouse_x = FB_WIDTH - 1;
    if (mouse_y >= FB_HEIGHT) mouse_y = FB_HEIGHT - 1;

    event.type = EVENT_MOUSE_MOVE;
    event.timestamp = (uint32_t)timer_get_ticks();
    event.data.mouse.x = mouse_x;
    event.data.mouse.y = mouse_y;
    event.data.mouse.buttons = mouse_buttons;

    event_push(&event);
}
```

Mouse position is tracked globally. Delta values are applied and clamped to screen bounds before generating the event.

### Keycode to ASCII Conversion

```c
static const char ascii_lower[] = {
    0, 0, 0, 0, 'a', 'b', 'c', 'd',  /* 0-7 */
    'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',  /* 8-15 */
    /* ... */
};

char keycode_to_ascii(keycode_t keycode, uint8_t mods)
{
    bool shift = (mods & MOD_SHIFT) || (mods & MOD_CAPSLOCK);

    if (keycode < sizeof(ascii_lower)) {
        if (shift) {
            /* Handle caps lock + shift cancellation for letters */
            if (keycode >= KEY_A && keycode <= KEY_Z) {
                if ((mods & MOD_SHIFT) && (mods & MOD_CAPSLOCK)) {
                    return ascii_lower[keycode];
                }
            }
            return ascii_upper[keycode];
        }
        return ascii_lower[keycode];
    }
    return 0;
}
```

Lookup tables map USB HID keycodes to ASCII. Shift and caps lock modify the lookup.

## Window Manager Implementation

### Window Manager State

```c
static struct {
    window_t *window_list;      /* Head of window list (bottom) */
    window_t *top_window;       /* Top window (focused) */
    window_t *focused;          /* Currently focused window */
    uint32_t window_count;
    bool initialized;
    bool should_exit;
    bool needs_redraw;

    /* Mouse state */
    int32_t mouse_x;
    int32_t mouse_y;
    bool mouse_visible;

    /* Dragging state */
    window_t *drag_window;
    int32_t drag_start_x;
    int32_t drag_start_y;

    /* Cursor backup buffer */
    uint32_t cursor_backup[CURSOR_WIDTH * CURSOR_HEIGHT];
} wm;
```

The window manager maintains a doubly-linked list of windows, cursor position, and dragging state.

### Main Loop

```c
void wm_run(void)
{
    event_t event;
    uint64_t last_update = 0;
    uint64_t now;

    while (!wm.should_exit) {
        /* Poll input devices */
        event_poll();
        virtio_input_poll();

        /* Process all pending events */
        while (event_pop(&event)) {
            wm_handle_event(&event);
        }

        /* Update display periodically (30 FPS) */
        now = timer_get_uptime_ms();
        if (now - last_update >= 33 || wm.needs_redraw) {
            wm_update_display();
            last_update = now;
        }

        /* Small delay to prevent busy looping */
        timer_delay_ms(1);
    }
}
```

The main loop polls input, processes events, and refreshes the display at approximately 30 FPS.

### Focus Management

```c
void wm_focus_window(window_t *win)
{
    if (!win || win == wm.focused) return;

    /* Remove focus from old window */
    if (wm.focused) {
        wm.focused->flags &= ~WINDOW_FLAG_FOCUSED;
        wm.focused->flags |= WINDOW_FLAG_DIRTY;
    }

    /* Move window to top of list */
    if (win != wm.top_window) {
        /* Remove from current position */
        if (win->prev) win->prev->next = win->next;
        else wm.window_list = win->next;
        if (win->next) win->next->prev = win->prev;

        /* Add to end (top) */
        win->prev = wm.top_window;
        win->next = NULL;
        if (wm.top_window) wm.top_window->next = win;
        wm.top_window = win;
    }

    /* Set focus */
    win->flags |= WINDOW_FLAG_FOCUSED | WINDOW_FLAG_DIRTY;
    wm.focused = win;
    wm.needs_redraw = true;
}
```

Focusing a window moves it to the top of the Z-order by relinking it in the doubly-linked list.

### Mouse Click Handling

```c
static void handle_mouse_button(mouse_event_t *mouse, bool pressed)
{
    window_t *win;

    if (pressed && (mouse->buttons & MOUSE_BUTTON_LEFT)) {
        win = wm_window_at(mouse->x, mouse->y);

        if (win) {
            wm_focus_window(win);

            /* Check close button */
            if (window_in_close_button(win, mouse->x, mouse->y)) {
                if (win->on_close) {
                    win->on_close(win);
                } else {
                    wm_unregister_window(win);
                    window_destroy(win);
                }
                return;
            }

            /* Check title bar for dragging */
            if (window_in_title_bar(win, mouse->x, mouse->y)) {
                wm.drag_window = win;
                wm.drag_start_x = mouse->x - win->x;
                wm.drag_start_y = mouse->y - win->y;
                win->flags |= WINDOW_FLAG_DRAGGING;
                return;
            }

            /* Pass to window callback */
            if (win->on_mouse) {
                mouse_event_t local = *mouse;
                local.x = mouse->x - win->client_x;
                local.y = mouse->y - win->client_y;
                win->on_mouse(win, &local);
            }
        } else {
            /* Click on desktop or taskbar */
            if (desktop_is_taskbar_click(mouse->y)) {
                desktop_handle_taskbar_click(mouse->x, mouse->y);
            } else {
                desktop_handle_click(mouse->x, mouse->y, false);
            }
            wm.needs_redraw = true;
        }
    }
}
```

Click handling checks windows from top to bottom. If no window is hit, the click goes to the desktop or taskbar.

### Cursor Rendering

```c
static const uint8_t cursor_bitmap[CURSOR_HEIGHT][CURSOR_WIDTH] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    /* ... arrow shape ... */
};

void wm_draw_cursor(void)
{
    /* Save background first */
    save_cursor_background(wm.mouse_x, wm.mouse_y);

    /* Draw cursor */
    for (j = 0; j < CURSOR_HEIGHT; j++) {
        for (i = 0; i < CURSOR_WIDTH; i++) {
            switch (cursor_bitmap[j][i]) {
                case 1: color = 0xFF000000; break;  /* Black outline */
                case 2: color = 0xFFFFFFFF; break;  /* White fill */
                default: continue;                   /* Transparent */
            }
            fb_putpixel(wm.mouse_x + i, wm.mouse_y + j, color);
        }
    }
}
```

The cursor is a 12x20 bitmap with black outline (1) and white fill (2). Background is saved before drawing and restored before the next frame.

## Window Implementation

### Window Structure

```c
typedef struct window {
    uint32_t id;
    char title[WINDOW_TITLE_MAX];
    int32_t x, y;
    uint32_t width, height;
    int32_t client_x, client_y;
    uint32_t client_width, client_height;
    uint32_t flags;
    int32_t z_order;

    /* Callbacks */
    void (*on_paint)(struct window *win);
    void (*on_key)(struct window *win, key_event_t *event);
    void (*on_mouse)(struct window *win, mouse_event_t *event);
    void (*on_close)(struct window *win);

    /* Linked list */
    struct window *next;
    struct window *prev;

    /* Optional backbuffer */
    uint32_t *backbuffer;
    uint32_t backbuffer_size;
} window_t;
```

Windows have both total dimensions (including decorations) and client area dimensions. Callbacks allow custom behavior.

### Client Area Calculation

```c
static void update_client_area(window_t *win)
{
    if (win->flags & WINDOW_FLAG_DECORATED) {
        win->client_x = win->x + WINDOW_BORDER_WIDTH;
        win->client_y = win->y + WINDOW_TITLE_HEIGHT;
        win->client_width = win->width - (2 * WINDOW_BORDER_WIDTH);
        win->client_height = win->height - WINDOW_TITLE_HEIGHT - WINDOW_BORDER_WIDTH;
    } else {
        win->client_x = win->x;
        win->client_y = win->y;
        win->client_width = win->width;
        win->client_height = win->height;
    }
}
```

The client area is the usable region inside window decorations.

### Window Drawing

```c
void window_draw(window_t *win)
{
    bool focused = (win->flags & WINDOW_FLAG_FOCUSED) != 0;

    /* Draw decorations */
    window_draw_decorations(win, focused);

    /* Fill client area with background */
    fb_fill_rect(win->client_x, win->client_y,
                 win->client_width, win->client_height,
                 WINDOW_CLIENT_BG);

    /* Call paint callback if set */
    if (win->on_paint) {
        win->on_paint(win);
    }

    win->flags &= ~WINDOW_FLAG_DIRTY;
}
```

Windows are drawn with decorations first, then the client area background, then the application's paint callback.

### Hit Testing

```c
bool window_in_close_button(window_t *win, int32_t x, int32_t y)
{
    int32_t close_x = win->x + win->width - WINDOW_CLOSE_BTN_SIZE - 2;
    int32_t close_y = win->y + 2;

    return (x >= close_x && x < close_x + WINDOW_CLOSE_BTN_SIZE &&
            y >= close_y && y < close_y + WINDOW_CLOSE_BTN_SIZE);
}

bool window_in_title_bar(window_t *win, int32_t x, int32_t y)
{
    return (x >= win->x && x < win->x + (int32_t)win->width &&
            y >= win->y && y < win->y + WINDOW_TITLE_HEIGHT);
}
```

Hit testing determines if a point is in the close button or title bar for click handling.

## Desktop Implementation

### Icon Layout

```c
int desktop_add_icon(const char *name, uint32_t icon_color, void (*on_launch)(void))
{
    /* Position in grid */
    col = desktop.icon_count % 2;  /* 2 columns */
    row = desktop.icon_count / 2;

    icon->x = ICON_MARGIN + col * ICON_SPACING;
    icon->y = ICON_MARGIN + row * (ICON_HEIGHT + ICON_LABEL_HEIGHT + 20);

    desktop.icon_count++;
    return desktop.icon_count - 1;
}
```

Icons are arranged in a 2-column grid with automatic positioning.

### Double-Click Detection

```c
void desktop_handle_click(int32_t x, int32_t y, bool double_click)
{
    uint64_t now = timer_get_uptime_ms();
    int icon_idx = find_icon_at(x, y);

    if (icon_idx >= 0) {
        /* Check for double-click */
        if (icon_idx == desktop.last_click_icon &&
            now - desktop.last_click_time < 500) {
            /* Double-click - launch */
            if (desktop.icons[icon_idx].on_launch) {
                desktop.icons[icon_idx].on_launch();
            }
        } else {
            /* Single click - select */
            desktop.selected_icon = icon_idx;
            desktop.icons[icon_idx].selected = true;
        }

        desktop.last_click_icon = icon_idx;
        desktop.last_click_time = now;
    }
}
```

Double-click is detected by comparing timestamps. If the same icon is clicked twice within 500ms, it's a double-click.

### Background Gradient

```c
void desktop_draw_background(void)
{
    uint32_t height = FB_HEIGHT - TASKBAR_HEIGHT;

    for (y = 0; y < height; y++) {
        /* Interpolate between top and bottom colors */
        uint32_t r = r1 + (r2 - r1) * y / height;
        uint32_t g = g1 + (g2 - g1) * y / height;
        uint32_t b = b1 + (b2 - b1) * y / height;

        uint32_t color = 0xFF000000 | (r << 16) | (g << 8) | b;
        fb_fill_rect(0, y, FB_WIDTH, 1, color);
    }
}
```

The gradient is drawn line by line with linear interpolation between top and bottom colors.

### Taskbar Drawing

```c
void desktop_draw_taskbar(void)
{
    /* Taskbar background */
    fb_fill_rect(0, taskbar_y, FB_WIDTH, TASKBAR_HEIGHT, TASKBAR_BG);

    /* Start button */
    fb_fill_rect(4, taskbar_y + 4, START_BUTTON_WIDTH, TASKBAR_HEIGHT - 8, START_BTN_BG);
    fb_puts(12, taskbar_y + 10, "AEOS", 0xFFFFFFFF, START_BTN_BG);

    /* Window buttons - iterate through window list */
    btn_x = START_BUTTON_WIDTH + 12;
    for (win = wm_get_window_list(); win != NULL; win = win->next) {
        if (!(win->flags & WINDOW_FLAG_VISIBLE)) continue;

        uint32_t btn_bg = (win->flags & WINDOW_FLAG_FOCUSED) ?
                          TASKBAR_BTN_ACTIVE : TASKBAR_BTN_BG;

        fb_fill_rect(btn_x, taskbar_y + 4, TASKBAR_BUTTON_WIDTH, TASKBAR_HEIGHT - 8, btn_bg);
        fb_puts(btn_x + 8, taskbar_y + 10, short_title, 0xFFFFFFFF, btn_bg);

        btn_x += TASKBAR_BUTTON_WIDTH + 4;
    }

    /* Clock - display uptime as HH:MM */
    fb_puts(FB_WIDTH - 50, taskbar_y + 10, time_str, CLOCK_COLOR, TASKBAR_BG);
}
```

The taskbar shows a Start button, buttons for each visible window, and a clock.

## GUI Integration

### Initialization Sequence

```c
int gui_init(void)
{
    /* Initialize event system first */
    event_init();

    /* Initialize VirtIO input devices */
    if (virtio_input_init() == 0) {
        klog_info("VirtIO input devices initialized");
    } else {
        klog_warn("VirtIO input devices not available, using UART fallback");
    }

    /* Initialize window manager */
    wm_init();

    /* Initialize desktop */
    desktop_init();

    /* Set desktop as the background paint callback */
    wm_set_desktop_paint(desktop_paint);

    /* Add desktop icons */
    desktop_add_icon("Terminal", 0xFF00AA00, launch_terminal_icon);
    desktop_add_icon("Files", 0xFFDDAA00, launch_filemanager_icon);
    desktop_add_icon("Settings", 0xFF6666AA, launch_settings_icon);
    desktop_add_icon("About", 0xFF0088CC, launch_about_icon);

    return 0;
}
```

GUI initialization follows a specific order: events, input, window manager, desktop, then icons.

### Application Launching

```c
static void launch_terminal_icon(void)
{
    gui_launch_terminal();
}

void gui_launch_terminal(void)
{
    terminal_t *term = terminal_create();
    if (!term) {
        klog_error("Failed to launch Terminal");
    }
}
```

Each icon has a launch callback that creates the corresponding application.

## Performance Considerations

### Dirty Region Optimization

The `needs_redraw` flag in the window manager prevents unnecessary full redraws. Only when events modify the display state is the flag set.

### Frame Rate Limiting

```c
if (now - last_update >= 33 || wm.needs_redraw) {
    wm_update_display();
    last_update = now;
}
```

Display updates are limited to ~30 FPS to reduce CPU usage. Forced redraws can still occur when `needs_redraw` is set.

### Cursor Optimization

```c
static void save_cursor_background(int32_t x, int32_t y)
{
    for (j = 0; j < CURSOR_HEIGHT; j++) {
        for (i = 0; i < CURSOR_WIDTH; i++) {
            wm.cursor_backup[j * CURSOR_WIDTH + i] = fb_getpixel(px, py);
        }
    }
}
```

The cursor background is saved and restored rather than redrawing the entire screen for cursor movement.

## Debugging Tips

### Event Tracing

```c
/* Add to wm_handle_event() */
kprintf("[EVENT] type=%d x=%d y=%d\n",
        event->type, event->data.mouse.x, event->data.mouse.y);
```

### Window List Debugging

```c
/* Dump window list */
for (win = wm.window_list; win; win = win->next) {
    kprintf("Window %u: '%s' at (%d,%d) flags=0x%x\n",
            win->id, win->title, win->x, win->y, win->flags);
}
```

### Focus State

```c
kprintf("Focused: %s\n", wm.focused ? wm.focused->title : "none");
kprintf("Top window: %s\n", wm.top_window ? wm.top_window->title : "none");
```
