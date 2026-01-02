/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/ramfs.h
 * Description: RAM-based filesystem (in-memory filesystem)
 * ============================================================================ */

#ifndef AEOS_RAMFS_H
#define AEOS_RAMFS_H

#include <aeos/vfs.h>
#include <aeos/types.h>

/* Maximum file size in ramfs (256 KB) */
#define RAMFS_MAX_FILE_SIZE  (256 * 1024)

/* Maximum files per directory */
#define RAMFS_MAX_FILES      64

/* RAM filesystem directory entry (internal) */
typedef struct ramfs_dirent {
    char name[64];              /* Entry name */
    struct vfs_inode *inode;    /* Pointer to child inode */
    struct ramfs_dirent *next;  /* Next entry */
} ramfs_dirent_t;

/* RAM filesystem inode data */
typedef struct ramfs_inode {
    char *data;                 /* File data buffer */
    size_t data_size;           /* Allocated data size */
    ramfs_dirent_t *entries;    /* Directory entries (if directory) */
    size_t num_entries;         /* Number of entries */
} ramfs_inode_t;

/**
 * Initialize ramfs filesystem
 * @return Pointer to ramfs filesystem structure
 */
vfs_filesystem_t *ramfs_create(void);

/**
 * Destroy ramfs filesystem
 */
void ramfs_destroy(vfs_filesystem_t *fs);

#endif /* AEOS_RAMFS_H */

/* ============================================================================
 * End of ramfs.h
 * ============================================================================ */
