/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/apps/filemanager.h
 * Description: File manager application
 * ============================================================================ */

#ifndef AEOS_APPS_FILEMANAGER_H
#define AEOS_APPS_FILEMANAGER_H

#include <aeos/types.h>
#include <aeos/window.h>

/* File list entry */
typedef struct {
    char name[64];
    bool is_directory;
    uint32_t size;
} file_entry_t;

/* File manager state */
typedef struct {
    window_t *window;
    char current_path[256];
    file_entry_t entries[64];
    uint32_t entry_count;
    int32_t selected_index;
    uint32_t scroll_offset;
    uint32_t visible_entries;
} filemanager_t;

/**
 * Create and show file manager window
 * @return File manager structure or NULL on error
 */
filemanager_t *filemanager_create(void);

/**
 * Destroy file manager
 */
void filemanager_destroy(filemanager_t *fm);

/**
 * Navigate to directory
 */
void filemanager_navigate(filemanager_t *fm, const char *path);

/**
 * Refresh file list
 */
void filemanager_refresh(filemanager_t *fm);

#endif /* AEOS_APPS_FILEMANAGER_H */
