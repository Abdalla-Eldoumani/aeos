/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/apps/calculator.c
 * Description: Four-function calculator. Parses what the user typed into a
 *              fixed-point int64 (10^6 scale), applies the operator, and
 *              formats the result back into the display string.
 * ============================================================================ */

#include <aeos/apps/calculator.h>
#include <aeos/window.h>
#include <aeos/wm.h>
#include <aeos/heap.h>
#include <aeos/string.h>
#include <aeos/kprintf.h>
#include <aeos/theme.h>

#define CALC_PAD            8
#define CALC_DISPLAY_H      36
#define CALC_DISPLAY_GAP    8
#define CALC_BTN_GAP        2
#define CALC_COLS           4
#define CALC_ROWS           5
#define CALC_DISPLAY_MAX   16

static const struct {
    const char *label;
    char        action;   /* digit char, '.', '+', '-', '*', '/', '=', 'C', 'N' (negate), 'P' (percent) */
    uint8_t     col;      /* starting column */
    uint8_t     row;
    uint8_t     span;     /* columns spanned (only the wide '0' uses 2) */
    bool        emphasis; /* operator buttons get accent text */
} CALC_BUTTONS[] = {
    { "C",  'C', 0, 0, 1, false },
    { "+/-",'N', 1, 0, 1, false },
    { "%",  'P', 2, 0, 1, false },
    { "/",  '/', 3, 0, 1, true  },

    { "7",  '7', 0, 1, 1, false },
    { "8",  '8', 1, 1, 1, false },
    { "9",  '9', 2, 1, 1, false },
    { "*",  '*', 3, 1, 1, true  },

    { "4",  '4', 0, 2, 1, false },
    { "5",  '5', 1, 2, 1, false },
    { "6",  '6', 2, 2, 1, false },
    { "-",  '-', 3, 2, 1, true  },

    { "1",  '1', 0, 3, 1, false },
    { "2",  '2', 1, 3, 1, false },
    { "3",  '3', 2, 3, 1, false },
    { "+",  '+', 3, 3, 1, true  },

    { "0",  '0', 0, 4, 2, false },
    { ".",  '.', 2, 4, 1, false },
    { "=",  '=', 3, 4, 1, true  }
};
#define CALC_BUTTON_COUNT (sizeof(CALC_BUTTONS) / sizeof(CALC_BUTTONS[0]))

static void calc_paint(window_t *win);
static void calc_mouse(window_t *win, mouse_event_t *mouse);
static void calc_close(window_t *win);

/**
 * Each button cell occupies a fixed pixel rect, computed from window dims.
 * Returns false if the col/row combination is out of bounds.
 */
static bool button_rect(uint8_t col, uint8_t row, uint8_t span,
                        int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    int32_t inner_w, inner_h, btn_w, btn_h, btns_top;

    inner_w = (int32_t)CALC_WIN_WIDTH
            - 2 * (int32_t)WINDOW_BORDER_WIDTH
            - 2 * CALC_PAD;
    inner_h = (int32_t)CALC_WIN_HEIGHT
            - (int32_t)WINDOW_TITLE_HEIGHT
            - (int32_t)WINDOW_BORDER_WIDTH
            - 2 * CALC_PAD;

    btn_w = (inner_w - (CALC_COLS - 1) * CALC_BTN_GAP) / CALC_COLS;
    btn_h = (inner_h - CALC_DISPLAY_H - CALC_DISPLAY_GAP
                     - (CALC_ROWS - 1) * CALC_BTN_GAP) / CALC_ROWS;
    btns_top = CALC_PAD + CALC_DISPLAY_H + CALC_DISPLAY_GAP;

    if (col + span > CALC_COLS || row >= CALC_ROWS) {
        return false;
    }

    *x = CALC_PAD + col * (btn_w + CALC_BTN_GAP);
    *y = btns_top + row * (btn_h + CALC_BTN_GAP);
    *w = span * btn_w + (span - 1) * CALC_BTN_GAP;
    *h = btn_h;
    return true;
}

/* ----------------------------------------------------------------------------
 * Display string parsing and formatting
 * -------------------------------------------------------------------------- */

/**
 * Parse the current display string into a fixed-point int64 (x10^6). Returns
 * false if the string isn't a valid number; the caller treats that as 0.
 */
static bool parse_display(const char *s, int64_t *out)
{
    int64_t value = 0;
    int     sign  = 1;
    int     decimals = 0;
    bool    saw_decimal = false;
    bool    saw_digit = false;

    if (s == NULL) return false;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }

    while (*s != '\0') {
        if (*s == '.') {
            if (saw_decimal) return false;
            saw_decimal = true;
            s++;
            continue;
        }
        if (*s < '0' || *s > '9') {
            return false;
        }
        if (saw_decimal) {
            if (decimals < 6) {
                value = value * 10 + (*s - '0');
                decimals++;
            }
            /* Past 6 decimals we silently truncate. */
        } else {
            value = value * 10 + (*s - '0');
        }
        saw_digit = true;
        s++;
    }
    if (!saw_digit) return false;

    /* Pad fractional out to 6 decimals so value is in 10^6 fixed-point. */
    while (decimals < 6) {
        value *= 10;
        decimals++;
    }
    *out = (int64_t)sign * value;
    return true;
}

/**
 * Format a fixed-point int64 (x10^6) into `dest` as "[-]int[.frac]". Trailing
 * fractional zeros are stripped. Truncates to fit `dest_size`.
 */
static void format_value(int64_t value, char *dest, size_t dest_size)
{
    char     buf[32];
    int      pos = 0;
    int64_t  abs_val;
    int64_t  int_part, frac_part;
    int      i;
    char     frac[7];
    int      frac_end;

    if (dest_size == 0) return;
    if (value < 0) {
        buf[pos++] = '-';
        abs_val = -value;
    } else {
        abs_val = value;
    }

    int_part  = abs_val / CALC_FP_SCALE;
    frac_part = abs_val % CALC_FP_SCALE;

    /* Integer part — write digits, then reverse in place. */
    int int_start = pos;
    if (int_part == 0) {
        buf[pos++] = '0';
    } else {
        char digits[24];
        int  d = 0;
        while (int_part > 0 && d < (int)sizeof(digits)) {
            digits[d++] = (char)('0' + (int)(int_part % 10));
            int_part /= 10;
        }
        while (d > 0) {
            buf[pos++] = digits[--d];
        }
    }
    (void)int_start;

    if (frac_part > 0) {
        /* Six fractional digits, MSD first. */
        for (i = 5; i >= 0; i--) {
            frac[i] = (char)('0' + (int)(frac_part % 10));
            frac_part /= 10;
        }
        frac[6] = '\0';
        /* Strip trailing zeros. */
        frac_end = 6;
        while (frac_end > 0 && frac[frac_end - 1] == '0') {
            frac_end--;
        }
        if (frac_end > 0) {
            buf[pos++] = '.';
            for (i = 0; i < frac_end && pos < (int)sizeof(buf) - 1; i++) {
                buf[pos++] = frac[i];
            }
        }
    }

    if ((size_t)pos >= dest_size) {
        pos = (int)dest_size - 1;
    }
    for (i = 0; i < pos; i++) {
        dest[i] = buf[i];
    }
    dest[pos] = '\0';
}

/* ----------------------------------------------------------------------------
 * Arithmetic
 * -------------------------------------------------------------------------- */

/**
 * Apply `op` to (a, b) where both are fixed-point int64 (x10^6). Sets *err
 * to true on divide by zero. Result is also x10^6 fixed-point.
 */
static int64_t apply_op(int64_t a, calc_op_t op, int64_t b, bool *err)
{
    int64_t r = 0;
    *err = false;
    switch (op) {
    case CALC_OP_ADD: r = a + b; break;
    case CALC_OP_SUB: r = a - b; break;
    case CALC_OP_MUL:
        /* a (x10^6) * b (x10^6) / 10^6 to keep fixed-point scale. */
        r = (a / CALC_FP_SCALE) * b
          + ((a % CALC_FP_SCALE) * b) / CALC_FP_SCALE;
        break;
    case CALC_OP_DIV:
        if (b == 0) { *err = true; return 0; }
        /* (a * 10^6) / b — split to avoid overflow on large operands. */
        r = (a / b) * CALC_FP_SCALE
          + ((a % b) * CALC_FP_SCALE) / b;
        break;
    case CALC_OP_NONE: r = b; break;
    }
    return r;
}

/* ----------------------------------------------------------------------------
 * Input handling
 * -------------------------------------------------------------------------- */

static void calc_reset(calculator_t *calc)
{
    calc->display[0] = '0';
    calc->display[1] = '\0';
    calc->prev       = 0;
    calc->pending    = CALC_OP_NONE;
    calc->fresh      = true;
    calc->error      = false;
}

static void append_char(calculator_t *calc, char c)
{
    size_t len = strlen(calc->display);
    if (len + 1 >= sizeof(calc->display) - 1) return;
    if (len >= CALC_DISPLAY_MAX) return;
    calc->display[len]     = c;
    calc->display[len + 1] = '\0';
}

static void apply_digit(calculator_t *calc, char digit)
{
    if (calc->error || calc->fresh) {
        calc->display[0] = digit;
        calc->display[1] = '\0';
        calc->fresh = false;
        calc->error = false;
        if (digit == '0') {
            /* Stay at "0" rather than appending another zero next time. */
            calc->fresh = true;
        }
        return;
    }
    /* Avoid leading-zero number like "0123". */
    if (calc->display[0] == '0' && calc->display[1] == '\0' && digit != '.') {
        calc->display[0] = digit;
        return;
    }
    append_char(calc, digit);
}

static void apply_decimal(calculator_t *calc)
{
    if (calc->error) {
        calc_reset(calc);
    }
    if (calc->fresh) {
        calc->display[0] = '0';
        calc->display[1] = '.';
        calc->display[2] = '\0';
        calc->fresh = false;
        return;
    }
    /* Reject if a decimal is already present. */
    if (strchr(calc->display, '.') != NULL) return;
    append_char(calc, '.');
}

static void apply_negate(calculator_t *calc)
{
    int64_t v;
    if (calc->error) return;
    if (!parse_display(calc->display, &v)) {
        return;
    }
    format_value(-v, calc->display, sizeof(calc->display));
    calc->fresh = false;
}

static void apply_percent(calculator_t *calc)
{
    int64_t v;
    if (calc->error) return;
    if (!parse_display(calc->display, &v)) return;
    format_value(v / 100, calc->display, sizeof(calc->display));
    calc->fresh = true;
}

static calc_op_t op_from_char(char c)
{
    switch (c) {
    case '+': return CALC_OP_ADD;
    case '-': return CALC_OP_SUB;
    case '*': return CALC_OP_MUL;
    case '/': return CALC_OP_DIV;
    default:  return CALC_OP_NONE;
    }
}

static void apply_operator(calculator_t *calc, char c)
{
    int64_t cur;
    bool    err;

    if (calc->error) return;
    if (!parse_display(calc->display, &cur)) return;

    if (calc->pending != CALC_OP_NONE && !calc->fresh) {
        /* Chain: compute prev OP cur, store result. */
        int64_t r = apply_op(calc->prev, calc->pending, cur, &err);
        if (err) {
            strcpy(calc->display, "Error");
            calc->error = true;
            return;
        }
        calc->prev = r;
        format_value(r, calc->display, sizeof(calc->display));
    } else {
        calc->prev = cur;
    }
    calc->pending = op_from_char(c);
    calc->fresh   = true;
}

static void apply_equals(calculator_t *calc)
{
    int64_t cur, r;
    bool    err;

    if (calc->error) return;
    if (calc->pending == CALC_OP_NONE) return;
    if (!parse_display(calc->display, &cur)) return;

    r = apply_op(calc->prev, calc->pending, cur, &err);
    if (err) {
        strcpy(calc->display, "Error");
        calc->error = true;
        return;
    }
    format_value(r, calc->display, sizeof(calc->display));
    calc->prev    = r;
    calc->pending = CALC_OP_NONE;
    calc->fresh   = true;
}

static void calc_dispatch(calculator_t *calc, char action)
{
    switch (action) {
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        apply_digit(calc, action);
        break;
    case '.': apply_decimal(calc); break;
    case 'C': calc_reset(calc); break;
    case 'N': apply_negate(calc); break;
    case 'P': apply_percent(calc); break;
    case '+': case '-': case '*': case '/':
        apply_operator(calc, action);
        break;
    case '=': apply_equals(calc); break;
    default: break;
    }
    if (calc->window) {
        window_invalidate(calc->window);
    }
}

/* ----------------------------------------------------------------------------
 * Window callbacks
 * -------------------------------------------------------------------------- */

static void calc_paint(window_t *win)
{
    calculator_t *calc = (calculator_t *)win->user_data;
    int32_t       x, y, w, h;
    size_t        i, len;
    int32_t       text_x, text_y;

    if (!calc) return;

    /* Background and display panel. */
    window_clear(win, THEME_SURFACE_1);
    window_fill_rect(win, CALC_PAD, CALC_PAD,
                     CALC_WIN_WIDTH - 2 * (int32_t)WINDOW_BORDER_WIDTH - 2 * CALC_PAD,
                     CALC_DISPLAY_H,
                     THEME_BG_DEEP);
    window_draw_rect(win, CALC_PAD, CALC_PAD,
                     CALC_WIN_WIDTH - 2 * (int32_t)WINDOW_BORDER_WIDTH - 2 * CALC_PAD,
                     CALC_DISPLAY_H,
                     THEME_BORDER_SUBTLE);

    /* Display string, right-aligned in 8x16. */
    len = strlen(calc->display);
    text_x = CALC_PAD + (CALC_WIN_WIDTH - 2 * (int32_t)WINDOW_BORDER_WIDTH - 2 * CALC_PAD)
           - (int32_t)(len * 8) - 8;
    if (text_x < CALC_PAD + 4) text_x = CALC_PAD + 4;
    text_y = CALC_PAD + (CALC_DISPLAY_H - 16) / 2;
    window_puts_large(win, text_x, text_y, calc->display,
                      calc->error ? THEME_DANGER : THEME_TEXT_PRIMARY,
                      THEME_BG_DEEP);

    /* Buttons. */
    for (i = 0; i < CALC_BUTTON_COUNT; i++) {
        if (!button_rect(CALC_BUTTONS[i].col, CALC_BUTTONS[i].row,
                         CALC_BUTTONS[i].span, &x, &y, &w, &h)) {
            continue;
        }
        window_fill_rect(win, x, y, w, h, THEME_SURFACE_2);
        window_draw_rect(win, x, y, w, h, THEME_BORDER_SUBTLE);

        size_t llen = strlen(CALC_BUTTONS[i].label);
        int32_t lx = x + (w - (int32_t)(llen * 8)) / 2;
        int32_t ly = y + (h - 16) / 2;
        uint32_t fg = CALC_BUTTONS[i].emphasis ? THEME_ACCENT : THEME_TEXT_PRIMARY;
        window_puts_large(win, lx, ly, CALC_BUTTONS[i].label, fg, THEME_SURFACE_2);
    }
}

static void calc_mouse(window_t *win, mouse_event_t *mouse)
{
    calculator_t *calc = (calculator_t *)win->user_data;
    int32_t       x, y, w, h;
    size_t        i;

    if (!calc) return;
    if (!(mouse->buttons & MOUSE_BUTTON_LEFT)) return;

    for (i = 0; i < CALC_BUTTON_COUNT; i++) {
        if (!button_rect(CALC_BUTTONS[i].col, CALC_BUTTONS[i].row,
                         CALC_BUTTONS[i].span, &x, &y, &w, &h)) {
            continue;
        }
        if (mouse->x >= x && mouse->x < x + w &&
            mouse->y >= y && mouse->y < y + h) {
            calc_dispatch(calc, CALC_BUTTONS[i].action);
            return;
        }
    }
}

static void calc_close(window_t *win)
{
    calculator_t *calc = (calculator_t *)win->user_data;

    win->on_paint = NULL;
    win->on_key   = NULL;
    win->on_mouse = NULL;
    win->on_close = NULL;

    wm_unregister_window(win);
    window_destroy(win);

    if (calc) {
        kfree(calc);
    }
}

/* ----------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

calculator_t *calculator_create(void)
{
    calculator_t *calc;

    calc = (calculator_t *)kmalloc(sizeof(calculator_t));
    if (!calc) {
        klog_error("Failed to allocate calculator");
        return NULL;
    }
    memset(calc, 0, sizeof(*calc));
    calc_reset(calc);

    calc->window = window_create("Calculator", 220, 80,
                                 CALC_WIN_WIDTH, CALC_WIN_HEIGHT,
                                 WINDOW_FLAG_VISIBLE);
    if (!calc->window) {
        kfree(calc);
        return NULL;
    }
    calc->window->on_paint = calc_paint;
    calc->window->on_mouse = calc_mouse;
    calc->window->on_close = calc_close;
    calc->window->user_data = calc;

    wm_register_window(calc->window);
    return calc;
}

void calculator_destroy(calculator_t *calc)
{
    if (!calc) return;
    /* Window destruction goes through the close path via the WM reaper. */
    kfree(calc);
}

/* ============================================================================
 * End of calculator.c
 * ============================================================================ */
