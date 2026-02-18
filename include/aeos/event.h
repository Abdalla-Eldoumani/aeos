/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/event.h
 * Description: Event queue system for input handling
 * ============================================================================ */

#ifndef AEOS_EVENT_H
#define AEOS_EVENT_H

#include <aeos/types.h>

/* Event queue size (must be large enough to avoid dropping button events
   during rapid mouse movement — each mouse move generates 1-2 events) */
#define EVENT_QUEUE_SIZE 256

/* Event types */
typedef enum {
    EVENT_NONE = 0,
    EVENT_KEY_DOWN,
    EVENT_KEY_UP,
    EVENT_MOUSE_MOVE,
    EVENT_MOUSE_BUTTON_DOWN,
    EVENT_MOUSE_BUTTON_UP,
    EVENT_MOUSE_SCROLL,
    EVENT_TIMER
} event_type_t;

/* Key codes (subset of USB HID key codes) */
typedef enum {
    KEY_NONE = 0,
    KEY_A = 4,
    KEY_B = 5,
    KEY_C = 6,
    KEY_D = 7,
    KEY_E = 8,
    KEY_F = 9,
    KEY_G = 10,
    KEY_H = 11,
    KEY_I = 12,
    KEY_J = 13,
    KEY_K = 14,
    KEY_L = 15,
    KEY_M = 16,
    KEY_N = 17,
    KEY_O = 18,
    KEY_P = 19,
    KEY_Q = 20,
    KEY_R = 21,
    KEY_S = 22,
    KEY_T = 23,
    KEY_U = 24,
    KEY_V = 25,
    KEY_W = 26,
    KEY_X = 27,
    KEY_Y = 28,
    KEY_Z = 29,
    KEY_1 = 30,
    KEY_2 = 31,
    KEY_3 = 32,
    KEY_4 = 33,
    KEY_5 = 34,
    KEY_6 = 35,
    KEY_7 = 36,
    KEY_8 = 37,
    KEY_9 = 38,
    KEY_0 = 39,
    KEY_ENTER = 40,
    KEY_ESCAPE = 41,
    KEY_BACKSPACE = 42,
    KEY_TAB = 43,
    KEY_SPACE = 44,
    KEY_MINUS = 45,
    KEY_EQUAL = 46,
    KEY_LEFTBRACE = 47,
    KEY_RIGHTBRACE = 48,
    KEY_BACKSLASH = 49,
    KEY_SEMICOLON = 51,
    KEY_APOSTROPHE = 52,
    KEY_GRAVE = 53,
    KEY_COMMA = 54,
    KEY_DOT = 55,
    KEY_SLASH = 56,
    KEY_CAPSLOCK = 57,
    KEY_F1 = 58,
    KEY_F2 = 59,
    KEY_F3 = 60,
    KEY_F4 = 61,
    KEY_F5 = 62,
    KEY_F6 = 63,
    KEY_F7 = 64,
    KEY_F8 = 65,
    KEY_F9 = 66,
    KEY_F10 = 67,
    KEY_F11 = 68,
    KEY_F12 = 69,
    KEY_RIGHT = 79,
    KEY_LEFT = 80,
    KEY_DOWN = 81,
    KEY_UP = 82,
    KEY_LEFTCTRL = 224,
    KEY_LEFTSHIFT = 225,
    KEY_LEFTALT = 226,
    KEY_LEFTMETA = 227,
    KEY_RIGHTCTRL = 228,
    KEY_RIGHTSHIFT = 229,
    KEY_RIGHTALT = 230,
    KEY_RIGHTMETA = 231
} keycode_t;

/* Mouse button codes */
typedef enum {
    MOUSE_BUTTON_NONE = 0,
    MOUSE_BUTTON_LEFT = 1,
    MOUSE_BUTTON_RIGHT = 2,
    MOUSE_BUTTON_MIDDLE = 4
} mouse_button_t;

/* Key modifier flags */
typedef enum {
    MOD_NONE = 0,
    MOD_SHIFT = (1 << 0),
    MOD_CTRL = (1 << 1),
    MOD_ALT = (1 << 2),
    MOD_META = (1 << 3),
    MOD_CAPSLOCK = (1 << 4)
} key_modifier_t;

/* Key event data */
typedef struct {
    keycode_t keycode;      /* Key code */
    char ascii;             /* ASCII character (if printable) */
    uint8_t modifiers;      /* Modifier flags */
} key_event_t;

/* Mouse event data */
typedef struct {
    int32_t x;              /* X position or delta */
    int32_t y;              /* Y position or delta */
    uint8_t buttons;        /* Button state (bitmask) */
    int8_t scroll;          /* Scroll delta */
} mouse_event_t;

/* Event structure */
typedef struct {
    event_type_t type;
    uint32_t timestamp;     /* Tick count when event occurred */
    union {
        key_event_t key;
        mouse_event_t mouse;
    } data;
} event_t;

/**
 * Initialize the event system
 */
void event_init(void);

/**
 * Push an event to the queue
 * @param event Event to push
 * @return true if successful, false if queue full
 */
bool event_push(const event_t *event);

/**
 * Pop an event from the queue
 * @param event Pointer to store event
 * @return true if event available, false if queue empty
 */
bool event_pop(event_t *event);

/**
 * Peek at the next event without removing it
 * @param event Pointer to store event
 * @return true if event available, false if queue empty
 */
bool event_peek(event_t *event);

/**
 * Check if event queue is empty
 */
bool event_queue_empty(void);

/**
 * Clear all events from queue
 */
void event_queue_clear(void);

/**
 * Poll input devices and generate events
 * Should be called from timer interrupt or main loop
 */
void event_poll(void);

/**
 * Get current mouse position
 */
void event_get_mouse_pos(int32_t *x, int32_t *y);

/**
 * Set mouse position (for mouse driver)
 */
void event_set_mouse_pos(int32_t x, int32_t y);

/**
 * Get current modifier key state
 */
uint8_t event_get_modifiers(void);

/**
 * Convert keycode to ASCII character
 */
char keycode_to_ascii(keycode_t keycode, uint8_t modifiers);

/**
 * Generate key event (used by input drivers)
 */
void event_generate_key(keycode_t keycode, bool pressed);

/**
 * Generate mouse move event (used by input drivers)
 */
void event_generate_mouse_move(int32_t dx, int32_t dy);

/**
 * Set absolute mouse position (used by tablet devices)
 * @param x X coordinate (-1 to keep current)
 * @param y Y coordinate (-1 to keep current)
 */
void event_set_mouse_position(int32_t x, int32_t y);

/**
 * Generate mouse button event (used by input drivers)
 */
void event_generate_mouse_button(uint8_t button, bool pressed);

#endif /* AEOS_EVENT_H */
