/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/apps/tetris.c
 * Description: Tetris. The board is 10x20 cells of 16 px each; the window is
 *              sized so a side panel fits next to it for score/level/lines/
 *              high score plus a NEXT preview. Gravity is wall-clock driven
 *              from inside on_paint, the same trick sysmon uses, with
 *              wm_request_redraw() at the end of every frame to keep the WM
 *              ticking without blocking input. High score persists to
 *              /tetris_high.bin via the VFS.
 * ============================================================================ */

#include <aeos/apps/tetris.h>
#include <aeos/wm.h>
#include <aeos/window.h>
#include <aeos/event.h>
#include <aeos/timer.h>
#include <aeos/heap.h>
#include <aeos/string.h>
#include <aeos/vfs.h>
#include <aeos/kprintf.h>
#include <aeos/theme.h>

/* ----- Layout ---------------------------------------------------------- */
#define TET_PAD          8
#define TET_BOARD_PX_W   (TETRIS_COLS * TETRIS_CELL_PX)
#define TET_BOARD_PX_H   (TETRIS_ROWS * TETRIS_CELL_PX)
#define TET_INFO_W       96
#define TET_CLIENT_W     (TET_BOARD_PX_W + 2 + TET_INFO_W + TET_PAD * 3)
#define TET_CLIENT_H     (TET_BOARD_PX_H + 2 + TET_PAD * 2)

/* ----- Tetromino shapes ------------------------------------------------ *
 * 4 rotations per shape, 16 bits per rotation laid out row-major (top
 * row in the high nibble). Bit 15 = (row 0, col 0); bit 0 = (row 3, col 3).
 *
 * Shape order matches the colour table below: I, O, T, S, Z, J, L. */
static const uint16_t TET_SHAPES[7][4] = {
    /* I */
    { 0x0F00, 0x2222, 0x00F0, 0x4444 },
    /* O */
    { 0x0660, 0x0660, 0x0660, 0x0660 },
    /* T */
    { 0x0E40, 0x4C40, 0x4E00, 0x4640 },
    /* S */
    { 0x06C0, 0x4620, 0x06C0, 0x4620 },
    /* Z */
    { 0x0C60, 0x2640, 0x0C60, 0x2640 },
    /* J */
    { 0x44C0, 0x8E00, 0x6440, 0x0E20 },
    /* L */
    { 0x4460, 0x0E80, 0xC440, 0x2E00 },
};

static const uint32_t TET_COLORS[7] = {
    0xFF00E0FFu, /* I cyan      */
    0xFFFFE000u, /* O yellow    */
    0xFFB05CFFu, /* T purple    */
    0xFF60E060u, /* S green     */
    0xFFE05050u, /* Z red       */
    0xFF5070FFu, /* J blue      */
    0xFFFF9040u, /* L orange    */
};

/* ----- Constants ------------------------------------------------------- */
#define TET_HIGH_SCORE_PATH "/tetris_high.bin"
#define TET_INITIAL_DROP_MS 800u
#define TET_MIN_DROP_MS     100u

/* Standard scoring per cleared lines (Nintendo): 1=100, 2=300, 3=500, 4=800,
 * each multiplied by current level. */
static const uint32_t TET_LINE_POINTS[5] = { 0, 100, 300, 500, 800 };

/* ----- PRNG ------------------------------------------------------------ */
static uint32_t tet_rand(tetris_t *t)
{
    /* xorshift64*. Seeded from uptime ms; falls back to a non-zero
     * constant if uptime is somehow zero so the state never goes dead. */
    uint64_t x = t->rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    t->rng_state = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1DULL) >> 32);
}

/* ----- Shape queries --------------------------------------------------- */
static bool tet_shape_bit(uint8_t type, uint8_t rot, int row, int col)
{
    uint16_t bits = TET_SHAPES[type][rot];
    int idx = row * 4 + col;          /* 0..15, top-left first */
    return (bits >> (15 - idx)) & 1;
}

static bool tet_can_place(const tetris_t *t,
                          uint8_t type, uint8_t rot,
                          int x, int y)
{
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (!tet_shape_bit(type, rot, r, c)) continue;
            int bx = x + c;
            int by = y + r;
            if (bx < 0 || bx >= TETRIS_COLS) return false;
            if (by >= TETRIS_ROWS)            return false;
            /* by < 0 is fine — pieces spawn partly above the board. */
            if (by >= 0 && t->board[by][bx]) return false;
        }
    }
    return true;
}

static void tet_lock_piece(tetris_t *t)
{
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (!tet_shape_bit(t->piece_type, t->piece_rot, r, c)) continue;
            int bx = t->piece_x + c;
            int by = t->piece_y + r;
            if (by >= 0 && by < TETRIS_ROWS && bx >= 0 && bx < TETRIS_COLS) {
                t->board[by][bx] = (uint8_t)(t->piece_type + 1);
            }
        }
    }
}

static int tet_clear_lines(tetris_t *t)
{
    int cleared = 0;
    for (int r = TETRIS_ROWS - 1; r >= 0; r--) {
        bool full = true;
        for (int c = 0; c < TETRIS_COLS; c++) {
            if (!t->board[r][c]) { full = false; break; }
        }
        if (!full) continue;

        for (int rr = r; rr > 0; rr--) {
            for (int c = 0; c < TETRIS_COLS; c++) {
                t->board[rr][c] = t->board[rr - 1][c];
            }
        }
        for (int c = 0; c < TETRIS_COLS; c++) t->board[0][c] = 0;
        cleared++;
        r++;  /* re-examine this row index after the shift */
    }
    return cleared;
}

static void tet_spawn_piece(tetris_t *t)
{
    t->piece_type = t->next_type;
    t->piece_rot  = 0;
    t->piece_x    = TETRIS_COLS / 2 - 2;
    t->piece_y    = -1;                 /* top of board, partly above */
    t->next_type  = (uint8_t)(tet_rand(t) % 7);

    if (!tet_can_place(t, t->piece_type, t->piece_rot,
                       t->piece_x, t->piece_y)) {
        t->game_over = true;
    }
}

/* ----- High-score persistence ----------------------------------------- */
static uint32_t tet_load_high_score(void)
{
    int fd = vfs_open(TET_HIGH_SCORE_PATH, O_RDONLY, 0);
    if (fd < 0) return 0;

    uint32_t value = 0;
    vfs_read(fd, &value, sizeof(value));
    vfs_close(fd);
    return value;
}

static void tet_save_high_score(uint32_t value)
{
    int fd = vfs_open(TET_HIGH_SCORE_PATH,
                      O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) return;

    vfs_write(fd, &value, sizeof(value));
    vfs_close(fd);
}

/* ----- Game step ------------------------------------------------------- */
static void tet_drop_one(tetris_t *t)
{
    if (t->game_over || t->paused) return;

    if (tet_can_place(t, t->piece_type, t->piece_rot,
                      t->piece_x, t->piece_y + 1)) {
        t->piece_y++;
    } else {
        tet_lock_piece(t);
        int n = tet_clear_lines(t);
        if (n > 0) {
            t->score += TET_LINE_POINTS[n] * t->level;
            t->lines_cleared += (uint32_t)n;
            uint32_t new_level = 1u + t->lines_cleared / 10u;
            if (new_level != t->level) {
                t->level = new_level;
                /* Speed up: 50 ms faster per level, floored. */
                if (t->drop_interval_ms > TET_MIN_DROP_MS + 50u) {
                    t->drop_interval_ms -= 50u;
                } else {
                    t->drop_interval_ms = TET_MIN_DROP_MS;
                }
            }
        }
        if (t->score > t->high_score) {
            t->high_score = t->score;
        }
        tet_spawn_piece(t);
    }
    t->last_drop_ms = timer_get_uptime_ms();
}

static void tet_reset(tetris_t *t)
{
    memset(t->board, 0, sizeof(t->board));
    t->score = 0;
    t->lines_cleared = 0;
    t->level = 1;
    t->drop_interval_ms = TET_INITIAL_DROP_MS;
    t->game_over = false;
    t->paused = false;
    t->next_type = (uint8_t)(tet_rand(t) % 7);
    tet_spawn_piece(t);
    t->last_drop_ms = timer_get_uptime_ms();
}

/* ----- Rendering ------------------------------------------------------- */
static void tet_draw_cell(window_t *win, int x, int y, uint32_t color)
{
    /* Solid centre + dark outline so blocks read as discrete tiles even at
     * 16 px. The outline doubles as the grid line. */
    window_fill_rect(win, x, y, TETRIS_CELL_PX, TETRIS_CELL_PX, color);
    window_draw_rect(win, x, y, TETRIS_CELL_PX, TETRIS_CELL_PX, 0xFF000000);
}

/* Print a uint into a tiny buffer. The 8x16 font here doesn't get a proper
 * snprintf "%u" path because the lib snprintf doesn't take ll-modifiers and
 * we want this to stay deterministic. Returns chars written. */
static int tet_format_u32(uint32_t v, char *buf, int max)
{
    char tmp[12];
    int  n = 0;

    if (max <= 0) return 0;
    if (v == 0) {
        if (max > 1) buf[n++] = '0';
        buf[n] = '\0';
        return n;
    }
    int t = 0;
    while (v > 0 && t < (int)sizeof(tmp)) {
        tmp[t++] = (char)('0' + (int)(v % 10u));
        v /= 10u;
    }
    while (t > 0 && n < max - 1) {
        buf[n++] = tmp[--t];
    }
    buf[n] = '\0';
    return n;
}

static void tetris_paint(window_t *win)
{
    tetris_t *t = (tetris_t *)win->user_data;
    if (t == NULL) return;

    /* Drive gravity from inside paint. wm_request_redraw at the end of the
     * function keeps the WM scheduler ticking even when no input arrives;
     * the actual drop logic is gated by elapsed wall-clock ms. */
    if (!t->game_over && !t->paused) {
        uint64_t now = timer_get_uptime_ms();
        if (now - t->last_drop_ms >= t->drop_interval_ms) {
            tet_drop_one(t);
        }
    }

    window_clear(win, THEME_SURFACE_1);

    int bx = TET_PAD;
    int by = TET_PAD;

    /* Board frame and background. */
    window_draw_rect(win, bx, by,
                     TET_BOARD_PX_W + 2, TET_BOARD_PX_H + 2,
                     THEME_BORDER_STRONG);
    window_fill_rect(win, bx + 1, by + 1,
                     TET_BOARD_PX_W, TET_BOARD_PX_H,
                     THEME_BG_DEEP);

    /* Locked cells. */
    for (int r = 0; r < TETRIS_ROWS; r++) {
        for (int c = 0; c < TETRIS_COLS; c++) {
            uint8_t v = t->board[r][c];
            if (!v) continue;
            tet_draw_cell(win,
                          bx + 1 + c * TETRIS_CELL_PX,
                          by + 1 + r * TETRIS_CELL_PX,
                          TET_COLORS[v - 1]);
        }
    }

    /* Active piece. */
    if (!t->game_over) {
        uint32_t color = TET_COLORS[t->piece_type];
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                if (!tet_shape_bit(t->piece_type, t->piece_rot, r, c)) continue;
                int cy = t->piece_y + r;
                int cx = t->piece_x + c;
                if (cy < 0) continue;       /* hide rows above the playfield */
                tet_draw_cell(win,
                              bx + 1 + cx * TETRIS_CELL_PX,
                              by + 1 + cy * TETRIS_CELL_PX,
                              color);
            }
        }
    }

    /* Info panel. */
    int ix = bx + TET_BOARD_PX_W + 2 + TET_PAD;
    int iy = by + 2;
    char buf[16];

    window_puts(win, ix, iy, "SCORE", THEME_TEXT_SECONDARY, THEME_SURFACE_1);
    iy += 10;
    tet_format_u32(t->score, buf, sizeof(buf));
    window_puts_large(win, ix, iy, buf, THEME_ACCENT, THEME_SURFACE_1);
    iy += 22;

    window_puts(win, ix, iy, "LEVEL", THEME_TEXT_SECONDARY, THEME_SURFACE_1);
    iy += 10;
    tet_format_u32(t->level, buf, sizeof(buf));
    window_puts_large(win, ix, iy, buf, THEME_SUCCESS, THEME_SURFACE_1);
    iy += 22;

    window_puts(win, ix, iy, "LINES", THEME_TEXT_SECONDARY, THEME_SURFACE_1);
    iy += 10;
    tet_format_u32(t->lines_cleared, buf, sizeof(buf));
    window_puts_large(win, ix, iy, buf, THEME_TEXT_PRIMARY, THEME_SURFACE_1);
    iy += 22;

    window_puts(win, ix, iy, "HIGH",  THEME_TEXT_SECONDARY, THEME_SURFACE_1);
    iy += 10;
    tet_format_u32(t->high_score, buf, sizeof(buf));
    window_puts_large(win, ix, iy, buf, THEME_WARNING, THEME_SURFACE_1);
    iy += 22;

    /* NEXT preview, half-size cells so a 4x4 piece fits in the side panel. */
    window_puts(win, ix, iy, "NEXT", THEME_TEXT_SECONDARY, THEME_SURFACE_1);
    iy += 10;
    int nb = TETRIS_CELL_PX / 2;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (!tet_shape_bit(t->next_type, 0, r, c)) continue;
            window_fill_rect(win,
                             ix + c * nb, iy + r * nb,
                             nb, nb,
                             TET_COLORS[t->next_type]);
            window_draw_rect(win,
                             ix + c * nb, iy + r * nb,
                             nb, nb,
                             0xFF000000);
        }
    }
    iy += nb * 4 + 6;

    if (t->game_over) {
        window_puts(win, ix, iy,     "GAME OVER", THEME_DANGER, THEME_SURFACE_1);
        window_puts(win, ix, iy + 12, "R = NEW",  THEME_TEXT_PRIMARY, THEME_SURFACE_1);
    } else if (t->paused) {
        window_puts(win, ix, iy, "PAUSED", THEME_WARNING, THEME_SURFACE_1);
    }

    /* Keep the redraw loop alive so gravity ticks even with no input. */
    wm_request_redraw();
}

/* ----- Input ----------------------------------------------------------- */
static void tetris_key(window_t *win, key_event_t *key)
{
    tetris_t *t = (tetris_t *)win->user_data;
    if (t == NULL || key == NULL) return;

    if (t->game_over) {
        if (key->ascii == 'r' || key->ascii == 'R') {
            tet_reset(t);
            window_invalidate(win);
        }
        return;
    }

    if (key->ascii == 'p' || key->ascii == 'P') {
        t->paused = !t->paused;
        if (!t->paused) {
            /* Resync gravity on resume so the next drop is on its own
             * full interval, not the leftover from before pause. */
            t->last_drop_ms = timer_get_uptime_ms();
        }
        window_invalidate(win);
        return;
    }

    if (t->paused) return;

    switch (key->keycode) {
    case KEY_LEFT:
        if (tet_can_place(t, t->piece_type, t->piece_rot,
                          t->piece_x - 1, t->piece_y)) {
            t->piece_x--;
            window_invalidate(win);
        }
        break;
    case KEY_RIGHT:
        if (tet_can_place(t, t->piece_type, t->piece_rot,
                          t->piece_x + 1, t->piece_y)) {
            t->piece_x++;
            window_invalidate(win);
        }
        break;
    case KEY_DOWN:
        /* Soft drop: one row, +1 score, reset gravity timer so we don't
         * double-drop on the next paint. */
        if (tet_can_place(t, t->piece_type, t->piece_rot,
                          t->piece_x, t->piece_y + 1)) {
            t->piece_y++;
            t->score++;
            t->last_drop_ms = timer_get_uptime_ms();
            window_invalidate(win);
        }
        break;
    case KEY_UP: {
        uint8_t new_rot = (uint8_t)((t->piece_rot + 1) & 3);
        if (tet_can_place(t, t->piece_type, new_rot,
                          t->piece_x, t->piece_y)) {
            t->piece_rot = new_rot;
            window_invalidate(win);
        }
        break;
    }
    case KEY_SPACE: {
        /* Hard drop: send the piece all the way down, +2 per row, then
         * lock immediately. */
        int rows = 0;
        while (tet_can_place(t, t->piece_type, t->piece_rot,
                             t->piece_x, t->piece_y + 1)) {
            t->piece_y++;
            rows++;
        }
        t->score += (uint32_t)(rows * 2);
        tet_drop_one(t);   /* triggers lock + clear + spawn */
        window_invalidate(win);
        break;
    }
    default:
        break;
    }
}

/* ----- Lifecycle ------------------------------------------------------- */
static void tetris_close(window_t *win)
{
    tetris_t *t = (tetris_t *)win->user_data;
    if (t == NULL) return;

    /* Persist the high score on close. We do NOT save during normal play
     * to keep VFS writes off the hot path; the in-memory high_score is
     * already kept up to date by tet_drop_one. */
    if (t->high_score > 0) {
        tet_save_high_score(t->high_score);
    }

    win->on_paint = NULL;
    win->on_key   = NULL;
    win->on_mouse = NULL;
    win->on_close = NULL;

    wm_unregister_window(win);
    window_destroy(win);
    kfree(t);
}

tetris_t *tetris_create(void)
{
    tetris_t *t = (tetris_t *)kmalloc(sizeof(*t));
    if (t == NULL) return NULL;
    memset(t, 0, sizeof(*t));

    t->window = window_create("Tetris", 80, 40,
                              TET_CLIENT_W, TET_CLIENT_H,
                              WINDOW_FLAG_VISIBLE | WINDOW_FLAG_DECORATED);
    if (t->window == NULL) {
        kfree(t);
        return NULL;
    }

    t->window->user_data = t;
    t->window->on_paint  = tetris_paint;
    t->window->on_key    = tetris_key;
    t->window->on_close  = tetris_close;

    /* Seed the PRNG. CNTVCT-derived ms is a small but real source of
     * variation between runs; we OR in a constant so a zero clock still
     * gives a non-zero state. */
    t->rng_state = (timer_get_uptime_ms() * 0x9E3779B97F4A7C15ULL) | 1ULL;

    t->level             = 1;
    t->drop_interval_ms  = TET_INITIAL_DROP_MS;
    t->next_type         = (uint8_t)(tet_rand(t) % 7);
    t->high_score        = tet_load_high_score();

    tet_spawn_piece(t);
    t->last_drop_ms = timer_get_uptime_ms();

    wm_register_window(t->window);
    return t;
}

void tetris_destroy(tetris_t *t)
{
    if (t != NULL && t->window != NULL) {
        tetris_close(t->window);
    }
}

/* ============================================================================
 * End of tetris.c
 * ============================================================================ */
