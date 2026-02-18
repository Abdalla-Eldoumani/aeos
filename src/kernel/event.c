/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/kernel/event.c
 * Description: Event queue system implementation
 * ============================================================================ */

#include <aeos/event.h>
#include <aeos/timer.h>
#include <aeos/uart.h>
#include <aeos/kprintf.h>
#include <aeos/string.h>
#include <aeos/framebuffer.h>

/* Event queue */
static event_t event_queue[EVENT_QUEUE_SIZE];
static uint32_t queue_head = 0;
static uint32_t queue_tail = 0;
static uint32_t queue_count = 0;

/* Mouse state */
static int32_t mouse_x = 320;  /* Start at center */
static int32_t mouse_y = 240;
static uint8_t mouse_buttons = 0;

/* Keyboard modifier state */
static uint8_t modifiers = 0;

/* ASCII lookup tables */
static const char ascii_lower[] = {
    0, 0, 0, 0, 'a', 'b', 'c', 'd',  /* 0-7 */
    'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',  /* 8-15 */
    'm', 'n', 'o', 'p', 'q', 'r', 's', 't',  /* 16-23 */
    'u', 'v', 'w', 'x', 'y', 'z', '1', '2',  /* 24-31 */
    '3', '4', '5', '6', '7', '8', '9', '0',  /* 32-39 */
    '\n', 0x1B, '\b', '\t', ' ', '-', '=', '[',  /* 40-47: enter, esc, bs, tab, space, -, =, [ */
    ']', '\\', 0, ';', '\'', '`', ',', '.',  /* 48-55: ], \, #, ;, ', `, ,, . */
    '/', 0  /* 56-57: /, caps */
};

static const char ascii_upper[] = {
    0, 0, 0, 0, 'A', 'B', 'C', 'D',  /* 0-7 */
    'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',  /* 8-15 */
    'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',  /* 16-23 */
    'U', 'V', 'W', 'X', 'Y', 'Z', '!', '@',  /* 24-31 */
    '#', '$', '%', '^', '&', '*', '(', ')',  /* 32-39 */
    '\n', 0x1B, '\b', '\t', ' ', '_', '+', '{',  /* 40-47 */
    '}', '|', 0, ':', '"', '~', '<', '>',  /* 48-55 */
    '?', 0  /* 56-57 */
};

/**
 * Initialize the event system
 */
void event_init(void)
{
    klog_info("Initializing event system...");

    queue_head = 0;
    queue_tail = 0;
    queue_count = 0;

    /* Initialize mouse at center of screen */
    mouse_x = FB_WIDTH / 2;
    mouse_y = FB_HEIGHT / 2;
    mouse_buttons = 0;

    modifiers = 0;

    klog_info("Event system initialized");
}

/**
 * Push an event to the queue
 * Uses memcpy instead of struct assignment for AArch64 safety with -O2
 */
bool event_push(const event_t *event)
{
    if (queue_count >= EVENT_QUEUE_SIZE) {
        return false;  /* Queue full */
    }

    memcpy(&event_queue[queue_tail], event, sizeof(event_t));
    queue_tail = (queue_tail + 1) % EVENT_QUEUE_SIZE;
    queue_count++;

    return true;
}

/**
 * Pop an event from the queue
 */
bool event_pop(event_t *event)
{
    if (queue_count == 0) {
        return false;  /* Queue empty */
    }

    memcpy(event, &event_queue[queue_head], sizeof(event_t));
    queue_head = (queue_head + 1) % EVENT_QUEUE_SIZE;
    queue_count--;

    return true;
}

/**
 * Peek at next event
 */
bool event_peek(event_t *event)
{
    if (queue_count == 0) {
        return false;
    }

    memcpy(event, &event_queue[queue_head], sizeof(event_t));
    return true;
}

/**
 * Check if queue is empty
 */
bool event_queue_empty(void)
{
    return queue_count == 0;
}

/**
 * Clear all events
 */
void event_queue_clear(void)
{
    queue_head = 0;
    queue_tail = 0;
    queue_count = 0;
}

/**
 * Convert keycode to ASCII
 */
char keycode_to_ascii(keycode_t keycode, uint8_t mods)
{
    bool shift = (mods & MOD_SHIFT) || (mods & MOD_CAPSLOCK);

    if (keycode < sizeof(ascii_lower)) {
        if (shift) {
            /* For letters, caps lock should toggle shift behavior */
            if (keycode >= KEY_A && keycode <= KEY_Z) {
                if ((mods & MOD_SHIFT) && (mods & MOD_CAPSLOCK)) {
                    return ascii_lower[keycode];  /* Both cancel out */
                }
            }
            return ascii_upper[keycode];
        }
        return ascii_lower[keycode];
    }

    return 0;
}

/**
 * Update modifier state from key event
 */
static void update_modifiers(keycode_t keycode, bool pressed)
{
    uint8_t mod = 0;

    switch (keycode) {
        case KEY_LEFTSHIFT:
        case KEY_RIGHTSHIFT:
            mod = MOD_SHIFT;
            break;
        case KEY_LEFTCTRL:
        case KEY_RIGHTCTRL:
            mod = MOD_CTRL;
            break;
        case KEY_LEFTALT:
        case KEY_RIGHTALT:
            mod = MOD_ALT;
            break;
        case KEY_LEFTMETA:
        case KEY_RIGHTMETA:
            mod = MOD_META;
            break;
        case KEY_CAPSLOCK:
            if (pressed) {
                modifiers ^= MOD_CAPSLOCK;  /* Toggle on press */
            }
            return;
        default:
            return;
    }

    if (pressed) {
        modifiers |= mod;
    } else {
        modifiers &= ~mod;
    }
}

/**
 * Process UART input (fallback keyboard)
 */
static void poll_uart_keyboard(void)
{
    while (uart_data_available()) {
        char c = uart_getc();
        event_t event;

        event.type = EVENT_KEY_DOWN;
        event.timestamp = (uint32_t)timer_get_ticks();
        event.data.key.modifiers = modifiers;
        event.data.key.ascii = c;

        /* Map ASCII to keycode (simplified) */
        if (c >= 'a' && c <= 'z') {
            event.data.key.keycode = KEY_A + (c - 'a');
        } else if (c >= 'A' && c <= 'Z') {
            event.data.key.keycode = KEY_A + (c - 'A');
            event.data.key.modifiers |= MOD_SHIFT;
        } else if (c >= '1' && c <= '9') {
            event.data.key.keycode = KEY_1 + (c - '1');
        } else if (c == '0') {
            event.data.key.keycode = KEY_0;
        } else if (c == '\r' || c == '\n') {
            event.data.key.keycode = KEY_ENTER;
            event.data.key.ascii = '\n';
        } else if (c == '\t') {
            event.data.key.keycode = KEY_TAB;
        } else if (c == ' ') {
            event.data.key.keycode = KEY_SPACE;
        } else if (c == 0x7F || c == '\b') {
            event.data.key.keycode = KEY_BACKSPACE;
            event.data.key.ascii = '\b';
        } else if (c == 0x1B) {
            event.data.key.keycode = KEY_ESCAPE;
        } else {
            event.data.key.keycode = KEY_NONE;
        }

        event_push(&event);

        /* Also push key up event immediately for UART */
        event.type = EVENT_KEY_UP;
        event_push(&event);
    }
}

/**
 * Poll all input devices
 */
void event_poll(void)
{
    /* Poll UART for keyboard input (fallback) */
    poll_uart_keyboard();

    /* VirtIO input polling is done in virtio_input.c if available */
}

/**
 * Get current mouse position
 */
void event_get_mouse_pos(int32_t *x, int32_t *y)
{
    if (x) *x = mouse_x;
    if (y) *y = mouse_y;
}

/**
 * Set mouse position
 */
void event_set_mouse_pos(int32_t x, int32_t y)
{
    /* Clamp to screen bounds */
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= FB_WIDTH) x = FB_WIDTH - 1;
    if (y >= FB_HEIGHT) y = FB_HEIGHT - 1;

    mouse_x = x;
    mouse_y = y;
}

/**
 * Get current modifiers
 */
uint8_t event_get_modifiers(void)
{
    return modifiers;
}

/**
 * Generate key event (used by input drivers)
 */
void event_generate_key(keycode_t keycode, bool pressed)
{
    event_t event;

    /* Update modifier state */
    update_modifiers(keycode, pressed);

    event.type = pressed ? EVENT_KEY_DOWN : EVENT_KEY_UP;
    event.timestamp = (uint32_t)timer_get_ticks();
    event.data.key.keycode = keycode;
    event.data.key.modifiers = modifiers;
    event.data.key.ascii = pressed ? keycode_to_ascii(keycode, modifiers) : 0;

    event_push(&event);
}

/**
 * Generate mouse move event (used by input drivers)
 */
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
    event.data.mouse.scroll = 0;

    event_push(&event);
}

/**
 * Set absolute mouse position (used by tablet devices)
 */
void event_set_mouse_position(int32_t x, int32_t y)
{
    event_t event;
    bool changed = false;

    /* Update position if specified (not -1) */
    if (x >= 0) {
        if (x >= FB_WIDTH) x = FB_WIDTH - 1;
        if (mouse_x != x) {
            mouse_x = x;
            changed = true;
        }
    }
    if (y >= 0) {
        if (y >= FB_HEIGHT) y = FB_HEIGHT - 1;
        if (mouse_y != y) {
            mouse_y = y;
            changed = true;
        }
    }

    /* Generate event if position changed */
    if (changed) {
        event.type = EVENT_MOUSE_MOVE;
        event.timestamp = (uint32_t)timer_get_ticks();
        event.data.mouse.x = mouse_x;
        event.data.mouse.y = mouse_y;
        event.data.mouse.buttons = mouse_buttons;
        event.data.mouse.scroll = 0;

        event_push(&event);
    }
}

/**
 * Generate mouse button event (used by input drivers)
 */
void event_generate_mouse_button(uint8_t button, bool pressed)
{
    event_t event;

    /* Update button state */
    if (pressed) {
        mouse_buttons |= button;
    } else {
        mouse_buttons &= ~button;
    }

    event.type = pressed ? EVENT_MOUSE_BUTTON_DOWN : EVENT_MOUSE_BUTTON_UP;
    event.timestamp = (uint32_t)timer_get_ticks();
    event.data.mouse.x = mouse_x;
    event.data.mouse.y = mouse_y;
    event.data.mouse.buttons = mouse_buttons;
    event.data.mouse.scroll = 0;

    event_push(&event);
}

/* ============================================================================
 * End of event.c
 * ============================================================================ */
