/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/fs_persist.h
 * Description: Filesystem persistence (save/load state)
 * ============================================================================ */

#ifndef AEOS_FS_PERSIST_H
#define AEOS_FS_PERSIST_H

#include <aeos/types.h>
#include <aeos/vfs.h>

/* Filesystem image magic number */
#define FS_MAGIC 0x41454F53 /* "AEOS" in hex (A=0x41, E=0x45, O=0x4F, S=0x53) */

/* Filesystem image version */
#define FS_VERSION 1

/* Maximum filesystem image size (2MB) */
#define FS_IMAGE_MAX_SIZE (2 * 1024 * 1024)

/* Filesystem image header */
typedef struct fs_image_header {
    uint32_t magic;         /* Magic number (FS_MAGIC) */
    uint32_t version;       /* Format version */
    uint64_t timestamp;     /* Save timestamp */
    uint32_t num_inodes;    /* Number of inodes */
    uint32_t data_size;     /* Total data size */
} fs_image_header_t;

/* Serialized inode entry */
typedef struct fs_inode_entry {
    uint64_t ino;           /* Inode number */
    uint32_t type;          /* File type */
    uint32_t mode;          /* Permissions */
    uint64_t size;          /* File size */
    uint64_t parent_ino;    /* Parent inode number (0 for root) */
    char name[64];          /* File/directory name */
    uint32_t data_offset;   /* Offset to file data in image */
    uint32_t num_children;  /* Number of children (for directories) */
} fs_inode_entry_t;

/**
 * Save filesystem to memory buffer
 * @param fs Filesystem to save
 * @param buffer Buffer to write to
 * @param buffer_size Size of buffer
 * @return Number of bytes written, or negative on error
 */
ssize_t fs_save(vfs_filesystem_t *fs, void *buffer, size_t buffer_size);

/**
 * Load filesystem from memory buffer
 * @param fs Filesystem to load into
 * @param buffer Buffer to read from
 * @param buffer_size Size of buffer
 * @return 0 on success, negative on error
 */
int fs_load(vfs_filesystem_t *fs, const void *buffer, size_t buffer_size);

/**
 * Get persistent storage buffer
 * @return Pointer to storage buffer
 */
void *fs_get_storage_buffer(void);

/**
 * Get persistent storage size
 * @return Size of storage buffer
 */
size_t fs_get_storage_size(void);

/**
 * Save filesystem to disk
 * @param fs Filesystem to save
 * @return 0 on success, negative on error
 */
int fs_save_to_disk(vfs_filesystem_t *fs);

/**
 * Load filesystem from disk
 * @param fs Filesystem to load into
 * @return 0 on success, negative on error
 */
int fs_load_from_disk(vfs_filesystem_t *fs);

#endif /* AEOS_FS_PERSIST_H */

/* ============================================================================
 * End of fs_persist.h
 * ============================================================================ */
