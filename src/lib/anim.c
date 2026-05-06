/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/lib/anim.c
 * Description: Animation easing helpers in Q0.8 fixed-point.
 * ============================================================================ */

#include <aeos/anim.h>
#include <aeos/types.h>

int32_t ease_out_cubic_q8(int32_t t_q8)
{
    int64_t u, u3;

    if (t_q8 <= 0) {
        return 0;
    }
    if (t_q8 >= ANIM_Q8_ONE) {
        return ANIM_Q8_ONE;
    }

    /* y = 1 - (1 - t)^3, all in Q0.8. (1-t) cubed leaves Q0.24 so divide twice
     * by ANIM_Q8_ONE to get back to Q0.8. */
    u = ANIM_Q8_ONE - t_q8;
    u3 = (u * u * u) / (ANIM_Q8_ONE * ANIM_Q8_ONE);
    return (int32_t)(ANIM_Q8_ONE - u3);
}

int32_t anim_progress_q8(uint64_t now_ms, uint64_t start_ms, uint32_t duration_ms)
{
    uint64_t elapsed;

    if (duration_ms == 0 || now_ms <= start_ms) {
        return 0;
    }
    elapsed = now_ms - start_ms;
    if (elapsed >= duration_ms) {
        return ANIM_Q8_ONE;
    }
    return (int32_t)((elapsed * ANIM_Q8_ONE) / duration_ms);
}

/* ============================================================================
 * End of anim.c
 * ============================================================================ */
