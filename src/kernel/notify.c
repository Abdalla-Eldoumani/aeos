/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/kernel/notify.c
 * Description: Floating toast notifications. Up to three toasts at once,
 *              top-right of the screen, slide in then fade out per the
 *              design system. Composited above windows in wm_run.
 * ============================================================================ */

#include <aeos/notify.h>
#include <aeos/framebuffer.h>
#include <aeos/theme.h>
#include <aeos/anim.h>
#include <aeos/timer.h>
#include <aeos/types.h>

#define NOTIFY_MAX             3
#define NOTIFY_WIDTH           280
#define NOTIFY_HEIGHT          48
#define NOTIFY_MARGIN          16
#define NOTIFY_GAP             8
#define NOTIFY_STRIPE_WIDTH    4
#define NOTIFY_TEXT_PAD_X      16
#define NOTIFY_TEXT_HEIGHT     16
#define NOTIFY_MSG_MAX         30  /* fits within 244-px text area at 8 px/char */

#define NOTIFY_SLIDE_MS        220u
#define NOTIFY_VISIBLE_MS      4000u
#define NOTIFY_FADE_MS         240u
#define NOTIFY_TOTAL_MS        (NOTIFY_SLIDE_MS + NOTIFY_VISIBLE_MS + NOTIFY_FADE_MS)

#define SCREEN_WIDTH           640

typedef struct {
    bool           in_use;
    notify_level_t level;
    char           message[NOTIFY_MSG_MAX + 1];
    uint64_t       created_ms;
} notify_t;

static notify_t toasts[NOTIFY_MAX];

void notify_init(void)
{
    int i;
    for (i = 0; i < NOTIFY_MAX; i++) {
        toasts[i].in_use = false;
        toasts[i].message[0] = '\0';
    }
}

static uint32_t stripe_color(notify_level_t level)
{
    switch (level) {
    case NOTIFY_INFO:  return THEME_ACCENT;
    case NOTIFY_WARN:  return THEME_WARNING;
    case NOTIFY_ERROR: return THEME_DANGER;
    }
    return THEME_ACCENT;
}

void notify_post(notify_level_t level, const char *msg)
{
    int      i, slot = -1;
    uint64_t oldest_t = (uint64_t)-1;
    size_t   len;

    if (msg == NULL) {
        return;
    }

    /* Prefer a free slot. If all three are taken, evict the oldest. */
    for (i = 0; i < NOTIFY_MAX; i++) {
        if (!toasts[i].in_use) {
            slot = i;
            break;
        }
        if (toasts[i].created_ms < oldest_t) {
            oldest_t = toasts[i].created_ms;
            slot = i;
        }
    }
    if (slot < 0) {
        return;
    }

    toasts[slot].in_use     = true;
    toasts[slot].level      = level;
    toasts[slot].created_ms = timer_get_uptime_ms();

    len = 0;
    while (msg[len] != '\0' && len < NOTIFY_MSG_MAX) {
        toasts[slot].message[len] = msg[len];
        len++;
    }
    toasts[slot].message[len] = '\0';
}

void notify_info(const char *msg)  { notify_post(NOTIFY_INFO,  msg); }
void notify_warn(const char *msg)  { notify_post(NOTIFY_WARN,  msg); }
void notify_error(const char *msg) { notify_post(NOTIFY_ERROR, msg); }

/**
 * Composite one toast at vertical stack slot `pos` (0 = top, newest). The
 * caller must already have evicted expired toasts and sorted by age.
 */
static void render_toast(const notify_t *t, int pos, uint64_t now)
{
    uint64_t elapsed;
    int32_t  target_x, target_y, cur_x;
    uint32_t fade_alpha = 0;
    int32_t  text_x, text_y;

    elapsed = now - t->created_ms;
    target_x = SCREEN_WIDTH - NOTIFY_WIDTH - NOTIFY_MARGIN;
    target_y = NOTIFY_MARGIN + pos * (NOTIFY_HEIGHT + NOTIFY_GAP);
    cur_x = target_x;

    if (elapsed < NOTIFY_SLIDE_MS) {
        int32_t t_q8 = anim_progress_q8(now, t->created_ms, NOTIFY_SLIDE_MS);
        int32_t eased = ease_out_cubic_q8(t_q8);
        cur_x = SCREEN_WIDTH
              + ((target_x - SCREEN_WIDTH) * eased) / ANIM_Q8_ONE;
    } else if (elapsed >= NOTIFY_SLIDE_MS + NOTIFY_VISIBLE_MS) {
        uint64_t fade_start = t->created_ms + NOTIFY_SLIDE_MS + NOTIFY_VISIBLE_MS;
        int32_t t_q8 = anim_progress_q8(now, fade_start, NOTIFY_FADE_MS);
        fade_alpha = (uint32_t)ease_out_cubic_q8(t_q8);
    }

    fb_fill_rect(cur_x, target_y, NOTIFY_WIDTH, NOTIFY_HEIGHT, THEME_SURFACE_2);
    fb_draw_rect(cur_x, target_y, NOTIFY_WIDTH, NOTIFY_HEIGHT, THEME_BORDER_STRONG);
    fb_fill_rect(cur_x + 1, target_y + 1,
                 NOTIFY_STRIPE_WIDTH, NOTIFY_HEIGHT - 2,
                 stripe_color(t->level));

    text_x = cur_x + 1 + NOTIFY_STRIPE_WIDTH + NOTIFY_TEXT_PAD_X;
    text_y = target_y + (NOTIFY_HEIGHT - NOTIFY_TEXT_HEIGHT) / 2;
    fb_puts_large(text_x, text_y, t->message,
                  THEME_TEXT_PRIMARY, THEME_SURFACE_2);

    if (fade_alpha > 0) {
        fb_blend_rect(cur_x, target_y, NOTIFY_WIDTH, NOTIFY_HEIGHT,
                      THEME_BG_DEEP, fade_alpha);
    }
}

void notify_render(void)
{
    int      active[NOTIFY_MAX];
    int      active_count = 0;
    int      i, j, tmp;
    uint64_t now, elapsed;

    now = timer_get_uptime_ms();

    for (i = 0; i < NOTIFY_MAX; i++) {
        if (!toasts[i].in_use) {
            continue;
        }
        elapsed = now - toasts[i].created_ms;
        if (elapsed >= NOTIFY_TOTAL_MS) {
            toasts[i].in_use = false;
            continue;
        }
        active[active_count++] = i;
    }

    /* Newest goes to the top of the stack. Tiny n; bubble sort is fine. */
    for (i = 0; i + 1 < active_count; i++) {
        for (j = i + 1; j < active_count; j++) {
            if (toasts[active[j]].created_ms > toasts[active[i]].created_ms) {
                tmp = active[i];
                active[i] = active[j];
                active[j] = tmp;
            }
        }
    }

    for (i = 0; i < active_count; i++) {
        render_toast(&toasts[active[i]], i, now);
    }
}

/* ============================================================================
 * End of notify.c
 * ============================================================================ */
