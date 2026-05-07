/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/apps/sysmon.c
 * Description: System monitor. Once per wall-second the paint hook samples
 *              the heap allocator into a 60-slot ring; the rest of each
 *              frame just renders the bars and the current totals.
 * ============================================================================ */

#include <aeos/apps/sysmon.h>
#include <aeos/window.h>
#include <aeos/wm.h>
#include <aeos/heap.h>
#include <aeos/timer.h>
#include <aeos/string.h>
#include <aeos/kprintf.h>
#include <aeos/theme.h>

#define SM_PAD             8
#define SM_HEADER_H       18
#define SM_HEADER_GAP      4
#define SM_BAR_WIDTH       4
#define SM_AXIS_LABEL_W   28

/* The 8x16 font ships only basic ASCII; format heap stats into a small
 * scratch buffer that fits comfortably in the 280-px window. */
#define SM_HEADER_BUF     48

static void sysmon_paint(window_t *win);
static void sysmon_close(window_t *win);

/**
 * Append `n` (truncated to KB) to dst with a "K" suffix. Returns chars written.
 */
static int format_kb(uint32_t bytes, char *dst, size_t dst_size)
{
    uint32_t kb = bytes / 1024;
    char buf[12];
    int  pos = 0;
    int  i, written;

    if (kb == 0) {
        buf[pos++] = '0';
    } else {
        char tmp[12];
        int  d = 0;
        while (kb > 0 && d < (int)sizeof(tmp)) {
            tmp[d++] = (char)('0' + (int)(kb % 10));
            kb /= 10;
        }
        while (d > 0) {
            buf[pos++] = tmp[--d];
        }
    }
    buf[pos++] = 'K';

    written = 0;
    for (i = 0; i < pos && (size_t)written < dst_size - 1; i++) {
        dst[written++] = buf[i];
    }
    dst[written] = '\0';
    return written;
}

static void sysmon_sample(sysmon_t *sm)
{
    heap_stats_t stats;
    uint32_t     pct;

    heap_get_stats(&stats);

    if (stats.total_size == 0) {
        pct = 0;
    } else {
        /* Avoid floating point: percentage in integer math. used_size is
         * size_t but always fits in uint32 for our 4 MB heap. */
        pct = (uint32_t)(((uint64_t)stats.used_size * 100u) / stats.total_size);
    }
    if (pct > 100) pct = 100;

    sm->history[sm->head] = (uint8_t)pct;
    sm->head = (uint8_t)((sm->head + 1) % SYSMON_HISTORY);
    if (sm->count < SYSMON_HISTORY) {
        sm->count++;
    }
}

static void sysmon_paint(window_t *win)
{
    sysmon_t    *sm = (sysmon_t *)win->user_data;
    heap_stats_t stats;
    uint64_t     now_sec;
    char         header[SM_HEADER_BUF];
    int          n;
    int32_t      graph_x, graph_y, graph_w, graph_h;
    int32_t      bar_h;
    uint32_t     i;
    int32_t      slot_x;
    uint8_t      pct;

    if (!sm) return;

    now_sec = timer_get_uptime_sec();
    if (now_sec != sm->last_sample_sec) {
        sm->last_sample_sec = now_sec;
        sysmon_sample(sm);
    }

    heap_get_stats(&stats);

    window_clear(win, THEME_SURFACE_1);

    /* Header: "Heap: 12K / 4096K (0%)" sized for a 280-px window. */
    n = 0;
    header[n++] = 'H'; header[n++] = 'e'; header[n++] = 'a'; header[n++] = 'p';
    header[n++] = ':'; header[n++] = ' ';
    n += format_kb((uint32_t)stats.used_size, header + n, sizeof(header) - (size_t)n);
    if ((size_t)n + 3 < sizeof(header)) {
        header[n++] = ' '; header[n++] = '/'; header[n++] = ' ';
    }
    n += format_kb((uint32_t)stats.total_size, header + n, sizeof(header) - (size_t)n);
    header[n] = '\0';

    window_puts_large(win, SM_PAD, SM_PAD, header,
                      THEME_TEXT_PRIMARY, THEME_SURFACE_1);

    graph_x = SM_PAD;
    graph_y = SM_PAD + SM_HEADER_H + SM_HEADER_GAP;
    graph_w = SYSMON_WIN_WIDTH
            - 2 * (int32_t)WINDOW_BORDER_WIDTH
            - 2 * SM_PAD;
    graph_h = SYSMON_WIN_HEIGHT
            - (int32_t)WINDOW_TITLE_HEIGHT
            - (int32_t)WINDOW_BORDER_WIDTH
            - 2 * SM_PAD - SM_HEADER_H - SM_HEADER_GAP;

    /* Graph background and frame */
    window_fill_rect(win, graph_x, graph_y, graph_w, graph_h, THEME_BG_DEEP);
    window_draw_rect(win, graph_x, graph_y, graph_w, graph_h, THEME_BORDER_SUBTLE);

    /* Center the SYSMON_HISTORY bars in the graph horizontally. Each bar is
     * SM_BAR_WIDTH px; older samples are on the left, newest on the right. */
    {
        int32_t bars_w = SYSMON_HISTORY * SM_BAR_WIDTH;
        int32_t inset_x = graph_x + (graph_w - bars_w) / 2;
        int32_t inset_y = graph_y + 2;
        int32_t inset_h = graph_h - 4;

        uint32_t first_used = (uint32_t)SYSMON_HISTORY - sm->count;
        for (i = first_used; i < SYSMON_HISTORY; i++) {
            uint32_t logical;   /* 0 = oldest in-history, count-1 = newest */
            uint32_t ring_idx;
            slot_x = inset_x + (int32_t)i * SM_BAR_WIDTH;
            logical = i - first_used;
            if (sm->count < SYSMON_HISTORY) {
                ring_idx = logical;
            } else {
                ring_idx = (sm->head + logical) % SYSMON_HISTORY;
            }
            pct = sm->history[ring_idx];
            if (pct > 100) pct = 100;
            bar_h = ((int32_t)pct * inset_h) / 100;
            if (bar_h > 0) {
                window_fill_rect(win,
                                 slot_x,
                                 inset_y + (inset_h - bar_h),
                                 SM_BAR_WIDTH - 1,
                                 (uint32_t)bar_h,
                                 THEME_ACCENT);
            }
        }
    }

    /* Drive the next frame so the once-per-second sample arrives. */
    wm_request_redraw();
}

static void sysmon_close(window_t *win)
{
    sysmon_t *sm = (sysmon_t *)win->user_data;

    win->on_paint = NULL;
    win->on_key   = NULL;
    win->on_mouse = NULL;
    win->on_close = NULL;

    wm_unregister_window(win);
    window_destroy(win);

    if (sm) {
        kfree(sm);
    }
}

sysmon_t *sysmon_create(void)
{
    sysmon_t *sm;

    sm = (sysmon_t *)kmalloc(sizeof(sysmon_t));
    if (!sm) {
        klog_error("Failed to allocate sysmon");
        return NULL;
    }
    memset(sm, 0, sizeof(*sm));
    sm->last_sample_sec = (uint64_t)-1;  /* trigger a sample on first paint */

    sm->window = window_create("System Monitor", 280, 100,
                               SYSMON_WIN_WIDTH, SYSMON_WIN_HEIGHT,
                               WINDOW_FLAG_VISIBLE);
    if (!sm->window) {
        kfree(sm);
        return NULL;
    }
    sm->window->on_paint = sysmon_paint;
    sm->window->on_close = sysmon_close;
    sm->window->user_data = sm;

    wm_register_window(sm->window);
    return sm;
}

void sysmon_destroy(sysmon_t *sm)
{
    if (!sm) return;
    kfree(sm);
}

/* ============================================================================
 * End of sysmon.c
 * ============================================================================ */
