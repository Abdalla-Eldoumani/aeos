/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/apps/notes.h
 * Description: Notes app — a simple GUI text editor that wraps the
 *              editor_t buffer engine and saves to /notes.txt by default.
 * ============================================================================ */

#ifndef AEOS_APPS_NOTES_H
#define AEOS_APPS_NOTES_H

#include <aeos/types.h>
#include <aeos/window.h>
#include <aeos/editor.h>

#define NOTES_WIN_WIDTH    360
#define NOTES_WIN_HEIGHT   300
#define NOTES_DEFAULT_PATH "/notes.txt"

typedef struct notes {
    window_t *window;
    editor_t  ed;             /* shared with the text-mode editor engine */
    bool      ed_initialized; /* editor_cleanup is only safe after init */
} notes_t;

notes_t *notes_create(void);
void     notes_destroy(notes_t *n);

#endif /* AEOS_APPS_NOTES_H */

/* ============================================================================
 * End of notes.h
 * ============================================================================ */
