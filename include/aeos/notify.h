/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/notify.h
 * Description: Floating toast notifications composited above the windows.
 * ============================================================================ */

#ifndef AEOS_NOTIFY_H
#define AEOS_NOTIFY_H

#include <aeos/types.h>

typedef enum {
    NOTIFY_INFO  = 0,
    NOTIFY_WARN  = 1,
    NOTIFY_ERROR = 2
} notify_level_t;

/**
 * Reset the toast list. Safe to call before the WM is up; toasts posted
 * before init are dropped.
 */
void notify_init(void);

/**
 * Post a toast at the given severity. Truncates the message to fit one row.
 * If three toasts are already on screen, the oldest is force-faded so the
 * new one can take its place.
 */
void notify_post(notify_level_t level, const char *msg);

/* Convenience wrappers — the names the tasks ask for. */
void notify_info(const char *msg);
void notify_warn(const char *msg);
void notify_error(const char *msg);

/**
 * Composite the active toasts onto the framebuffer. Call from the WM main
 * loop AFTER window compositing and BEFORE cursor compositing so toasts
 * float above windows but never above the cursor.
 */
void notify_render(void);

#endif /* AEOS_NOTIFY_H */

/* ============================================================================
 * End of notify.h
 * ============================================================================ */
