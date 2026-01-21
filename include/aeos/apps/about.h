/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/apps/about.h
 * Description: About dialog
 * ============================================================================ */

#ifndef AEOS_APPS_ABOUT_H
#define AEOS_APPS_ABOUT_H

#include <aeos/types.h>
#include <aeos/window.h>

/* About app state */
typedef struct {
    window_t *window;
} about_t;

/**
 * Create and show about dialog
 */
about_t *about_create(void);

/**
 * Destroy about dialog
 */
void about_destroy(about_t *about);

#endif /* AEOS_APPS_ABOUT_H */
