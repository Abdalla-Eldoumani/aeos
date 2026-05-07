/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/apps/calculator.h
 * Description: Four-function calculator app.
 * ============================================================================ */

#ifndef AEOS_APPS_CALCULATOR_H
#define AEOS_APPS_CALCULATOR_H

#include <aeos/types.h>
#include <aeos/window.h>

/* Window dims fixed by the design system; the button grid is laid out from
 * these tokens at runtime. */
#define CALC_WIN_WIDTH       200
#define CALC_WIN_HEIGHT      260

/* Internal numeric representation: int64 scaled by 10^6 (six decimal places).
 * Lets us avoid FP under -mgeneral-regs-only while still supporting decimals
 * for the four arithmetic ops the design system promises. */
#define CALC_FP_SCALE        1000000

typedef enum {
    CALC_OP_NONE = 0,
    CALC_OP_ADD,
    CALC_OP_SUB,
    CALC_OP_MUL,
    CALC_OP_DIV
} calc_op_t;

typedef struct calculator {
    window_t *window;
    /* Display string the user is editing or the most recent result. Up to
     * 16 visible chars; we leave room for a sign and a NUL. */
    char     display[20];
    int64_t  prev;          /* stored operand from the last operator press */
    calc_op_t pending;      /* operator waiting for the right-hand operand */
    bool     fresh;         /* next digit press starts a new number */
    bool     error;         /* divide-by-zero or overflow trips this */
} calculator_t;

calculator_t *calculator_create(void);
void          calculator_destroy(calculator_t *calc);

#endif /* AEOS_APPS_CALCULATOR_H */

/* ============================================================================
 * End of calculator.h
 * ============================================================================ */
