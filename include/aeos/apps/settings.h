/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/apps/settings.h
 * Description: Settings application
 * ============================================================================ */

#ifndef AEOS_APPS_SETTINGS_H
#define AEOS_APPS_SETTINGS_H

#include <aeos/types.h>
#include <aeos/window.h>

/* Settings app state */
typedef struct {
    window_t *window;
} settings_t;

/**
 * Create and show settings window
 */
settings_t *settings_create(void);

/**
 * Destroy settings window
 */
void settings_destroy(settings_t *settings);

#endif /* AEOS_APPS_SETTINGS_H */
