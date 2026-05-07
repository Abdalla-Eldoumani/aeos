/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/apps/sysmon.h
 * Description: System monitor — live heap-used graph over the last 60 seconds.
 * ============================================================================ */

#ifndef AEOS_APPS_SYSMON_H
#define AEOS_APPS_SYSMON_H

#include <aeos/types.h>
#include <aeos/window.h>

#define SYSMON_WIN_WIDTH    280
#define SYSMON_WIN_HEIGHT   180
#define SYSMON_HISTORY      60   /* one bar per second */

typedef struct sysmon {
    window_t *window;
    /* Ring buffer of heap-used percentages (0..100). head is the slot the
     * NEXT sample writes into. count is how many samples are valid. */
    uint8_t  history[SYSMON_HISTORY];
    uint8_t  head;
    uint8_t  count;
    /* Last second we sampled. Drives the once-per-second update inside the
     * paint callback. */
    uint64_t last_sample_sec;
} sysmon_t;

sysmon_t *sysmon_create(void);
void      sysmon_destroy(sysmon_t *sm);

#endif /* AEOS_APPS_SYSMON_H */

/* ============================================================================
 * End of sysmon.h
 * ============================================================================ */
