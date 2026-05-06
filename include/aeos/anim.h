/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/anim.h
 * Description: Animation easing helpers. All math is Q0.8 fixed-point because
 *              the kernel disables FP/SIMD via -mgeneral-regs-only.
 * ============================================================================ */

#ifndef AEOS_ANIM_H
#define AEOS_ANIM_H

#include <aeos/types.h>

/* Q0.8 fixed-point: 0 means start, 256 means end. */
#define ANIM_Q8_ONE 256

/**
 * Cubic ease-out: y = 1 - (1 - t)^3.
 *
 * @param t_q8 input position, 0..256 (Q0.8). Values outside the range clamp.
 * @return eased position 0..256 (Q0.8).
 */
int32_t ease_out_cubic_q8(int32_t t_q8);

/**
 * Convert (now_ms - start_ms) and a duration into Q0.8 progress, clamped 0..256.
 */
int32_t anim_progress_q8(uint64_t now_ms, uint64_t start_ms, uint32_t duration_ms);

#endif /* AEOS_ANIM_H */

/* ============================================================================
 * End of anim.h
 * ============================================================================ */
