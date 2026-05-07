/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/apps/tetris.h
 * Description: Tetris GUI app. Exercises the framebuffer, the keyboard event
 *              path, the timer (gravity), the window manager (it's just
 *              another window), and the VFS (high-score persistence).
 * ============================================================================ */

#ifndef AEOS_APPS_TETRIS_H
#define AEOS_APPS_TETRIS_H

#include <aeos/types.h>
#include <aeos/window.h>

#define TETRIS_COLS    10
#define TETRIS_ROWS    20
#define TETRIS_CELL_PX 16

typedef struct tetris {
    window_t *window;

    /* Locked board cells. 0 == empty; 1..7 == shape index + 1 (so the colour
     * table can be looked up directly). The active piece is rendered on top
     * of this without ever being written into the board until it locks. */
    uint8_t  board[TETRIS_ROWS][TETRIS_COLS];

    uint8_t  piece_type;     /* 0..6 */
    uint8_t  piece_rot;      /* 0..3 */
    int8_t   piece_x;        /* board column of the 4x4 shape origin */
    int8_t   piece_y;        /* board row    of the 4x4 shape origin */

    uint8_t  next_type;

    uint32_t score;
    uint32_t lines_cleared;
    uint32_t level;
    uint32_t high_score;

    bool     game_over;
    bool     paused;

    uint64_t last_drop_ms;
    uint32_t drop_interval_ms;

    /* Inline xorshift64 PRNG so we don't pull in a global RNG. */
    uint64_t rng_state;
} tetris_t;

tetris_t *tetris_create(void);
void      tetris_destroy(tetris_t *t);

#endif /* AEOS_APPS_TETRIS_H */

/* ============================================================================
 * End of tetris.h
 * ============================================================================ */
