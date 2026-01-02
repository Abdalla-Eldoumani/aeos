/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/fs/ramfs.c
 * Description: RAM-based filesystem implementation
 * ============================================================================ */

#include <aeos/ramfs.h>
#include <aeos/vfs.h>
#include <aeos/heap.h>
#include <aeos/kprintf.h>
#include <aeos/string.h>

/* Forward declarations */
static int ramfs_inode_lookup(vfs_inode_t *parent, const char *name, vfs_inode_t **result);
static int ramfs_inode_create(vfs_inode_t *parent, const char *name, uint32_t mode, vfs_inode_t **result);
static int ramfs_inode_mkdir(vfs_inode_t *parent, const char *name, uint32_t mode, vfs_inode_t **result);
static int ramfs_inode_unlink(vfs_inode_t *parent, const char *name);
static int ramfs_inode_rmdir(vfs_inode_t *parent, const char *name);
static ssize_t ramfs_file_read(vfs_file_t *file, void *buf, size_t count);
static ssize_t ramfs_file_write(vfs_file_t *file, const void *buf, size_t count);
static int ramfs_file_close(vfs_file_t *file);
static int ramfs_dir_readdir(vfs_file_t *file, vfs_dirent_t **dirent);

/* Ramfs operations */
static vfs_fs_ops_t ramfs_ops = {
    .inode_lookup = ramfs_inode_lookup,
    .inode_create = ramfs_inode_create,
    .inode_mkdir = ramfs_inode_mkdir,
    .inode_unlink = ramfs_inode_unlink,
    .inode_rmdir = ramfs_inode_rmdir,
    .file_read = ramfs_file_read,
    .file_write = ramfs_file_write,
    .file_close = ramfs_file_close,
    .dir_readdir = ramfs_dir_readdir,
};

/* Global inode counter */
static uint64_t next_ino = 1;

/**
 * Create a new ramfs inode
 */
static vfs_inode_t *ramfs_create_inode(vfs_file_type_t type, uint32_t mode)
{
    vfs_inode_t *inode;
    ramfs_inode_t *ramfs_data;

    /* Allocate VFS inode */
    inode = (vfs_inode_t *)kmalloc(sizeof(vfs_inode_t));
    if (inode == NULL) {
        return NULL;
    }

    /* Allocate ramfs-specific data */
    ramfs_data = (ramfs_inode_t *)kmalloc(sizeof(ramfs_inode_t));
    if (ramfs_data == NULL) {
        kfree(inode);
        return NULL;
    }

    /* Initialize inode */
    inode->ino = next_ino++;
    inode->type = type;
    inode->mode = mode;
    inode->uid = 0;
    inode->gid = 0;
    inode->size = 0;
    inode->blocks = 0;
    inode->atime = 0;
    inode->mtime = 0;
    inode->ctime = 0;
    inode->nlinks = 1;
    inode->fs_data = ramfs_data;
    inode->fs = NULL;  /* Set by caller */

    /* Initialize ramfs data */
    ramfs_data->data = NULL;
    ramfs_data->data_size = 0;
    ramfs_data->entries = NULL;
    ramfs_data->num_entries = 0;

    klog_debug("Created ramfs inode %llu (type=%d)", inode->ino, type);
    return inode;
}

/**
 * Lookup a file in a directory
 */
static int ramfs_inode_lookup(vfs_inode_t *parent, const char *name, vfs_inode_t **result)
{
    ramfs_inode_t *parent_data;
    ramfs_dirent_t *entry;

    if (parent == NULL || name == NULL || result == NULL) {
        return -1;
    }

    if (parent->type != VFS_FILE_DIRECTORY) {
        klog_error("ramfs_lookup: Parent is not a directory");
        return -1;
    }

    parent_data = (ramfs_inode_t *)parent->fs_data;

    /* Search directory entries */
    entry = parent_data->entries;
    while (entry != NULL) {
        if (strcmp(entry->name, name) == 0) {
            /* Found it */
            *result = entry->inode;
            klog_debug("ramfs_lookup: Found '%s' (ino=%llu)", name, entry->inode->ino);
            return 0;
        }
        entry = entry->next;
    }

    klog_debug("ramfs_lookup: Entry '%s' not found", name);
    return -1;
}

/**
 * Create a new file
 */
static int ramfs_inode_create(vfs_inode_t *parent, const char *name, uint32_t mode, vfs_inode_t **result)
{
    ramfs_inode_t *parent_data;
    ramfs_dirent_t *new_entry;
    vfs_inode_t *new_inode;

    if (parent == NULL || name == NULL || result == NULL) {
        return -1;
    }

    if (parent->type != VFS_FILE_DIRECTORY) {
        klog_error("ramfs_create: Parent is not a directory");
        return -1;
    }

    parent_data = (ramfs_inode_t *)parent->fs_data;

    /* Check if already exists */
    ramfs_dirent_t *entry = parent_data->entries;
    while (entry != NULL) {
        if (strcmp(entry->name, name) == 0) {
            klog_error("ramfs_create: File '%s' already exists", name);
            return -1;
        }
        entry = entry->next;
    }

    /* Check limit */
    if (parent_data->num_entries >= RAMFS_MAX_FILES) {
        klog_error("ramfs_create: Directory full");
        return -1;
    }

    /* Create new inode */
    new_inode = ramfs_create_inode(VFS_FILE_REGULAR, mode);
    if (new_inode == NULL) {
        klog_error("ramfs_create: Failed to create inode");
        return -1;
    }
    new_inode->fs = parent->fs;

    /* Create directory entry */
    new_entry = (ramfs_dirent_t *)kmalloc(sizeof(ramfs_dirent_t));
    if (new_entry == NULL) {
        kfree(new_inode->fs_data);
        kfree(new_inode);
        klog_error("ramfs_create: Failed to allocate entry");
        return -1;
    }

    strncpy(new_entry->name, name, sizeof(new_entry->name) - 1);
    new_entry->name[sizeof(new_entry->name) - 1] = '\0';
    new_entry->inode = new_inode;
    new_entry->next = parent_data->entries;

    /* Add to directory */
    parent_data->entries = new_entry;
    parent_data->num_entries++;

    *result = new_inode;
    klog_debug("ramfs_create: Created file '%s' (ino=%llu)", name, new_inode->ino);
    return 0;
}

/**
 * Create a new directory
 */
static int ramfs_inode_mkdir(vfs_inode_t *parent, const char *name, uint32_t mode, vfs_inode_t **result)
{
    ramfs_inode_t *parent_data;
    ramfs_dirent_t *new_entry;
    vfs_inode_t *new_inode;

    if (parent == NULL || name == NULL || result == NULL) {
        return -1;
    }

    if (parent->type != VFS_FILE_DIRECTORY) {
        klog_error("ramfs_mkdir: Parent is not a directory");
        return -1;
    }

    parent_data = (ramfs_inode_t *)parent->fs_data;

    /* Check if already exists */
    ramfs_dirent_t *entry = parent_data->entries;
    while (entry != NULL) {
        if (strcmp(entry->name, name) == 0) {
            klog_error("ramfs_mkdir: Directory '%s' already exists", name);
            return -1;
        }
        entry = entry->next;
    }

    /* Check limit */
    if (parent_data->num_entries >= RAMFS_MAX_FILES) {
        klog_error("ramfs_mkdir: Directory full");
        return -1;
    }

    /* Create new directory inode */
    new_inode = ramfs_create_inode(VFS_FILE_DIRECTORY, mode);
    if (new_inode == NULL) {
        klog_error("ramfs_mkdir: Failed to create inode");
        return -1;
    }
    new_inode->fs = parent->fs;

    /* Create directory entry */
    new_entry = (ramfs_dirent_t *)kmalloc(sizeof(ramfs_dirent_t));
    if (new_entry == NULL) {
        kfree(new_inode->fs_data);
        kfree(new_inode);
        klog_error("ramfs_mkdir: Failed to allocate entry");
        return -1;
    }

    strncpy(new_entry->name, name, sizeof(new_entry->name) - 1);
    new_entry->name[sizeof(new_entry->name) - 1] = '\0';
    new_entry->inode = new_inode;
    new_entry->next = parent_data->entries;

    /* Add to directory */
    parent_data->entries = new_entry;
    parent_data->num_entries++;

    *result = new_inode;
    klog_debug("ramfs_mkdir: Created directory '%s' (ino=%llu)", name, new_inode->ino);
    return 0;
}

/**
 * Delete a file
 */
static int ramfs_inode_unlink(vfs_inode_t *parent, const char *name)
{
    ramfs_inode_t *parent_data;
    ramfs_dirent_t *entry, *prev;
    ramfs_inode_t *child_data;

    if (parent == NULL || name == NULL) {
        return -1;
    }

    if (parent->type != VFS_FILE_DIRECTORY) {
        klog_error("ramfs_unlink: Parent is not a directory");
        return -1;
    }

    parent_data = (ramfs_inode_t *)parent->fs_data;

    /* Find the entry */
    prev = NULL;
    entry = parent_data->entries;
    while (entry != NULL) {
        if (strcmp(entry->name, name) == 0) {
            /* Found it - check it's not a directory */
            if (entry->inode->type == VFS_FILE_DIRECTORY) {
                klog_error("ramfs_unlink: '%s' is a directory (use rmdir)", name);
                return -1;
            }

            /* Remove from list */
            if (prev == NULL) {
                parent_data->entries = entry->next;
            } else {
                prev->next = entry->next;
            }
            parent_data->num_entries--;

            /* Free the inode and its data */
            child_data = (ramfs_inode_t *)entry->inode->fs_data;
            if (child_data->data != NULL) {
                kfree(child_data->data);
            }
            kfree(child_data);
            kfree(entry->inode);
            kfree(entry);

            klog_debug("ramfs_unlink: Deleted file '%s'", name);
            return 0;
        }
        prev = entry;
        entry = entry->next;
    }

    klog_error("ramfs_unlink: File '%s' not found", name);
    return -1;
}

/**
 * Delete a directory
 */
static int ramfs_inode_rmdir(vfs_inode_t *parent, const char *name)
{
    ramfs_inode_t *parent_data;
    ramfs_dirent_t *entry, *prev;
    ramfs_inode_t *child_data;

    if (parent == NULL || name == NULL) {
        return -1;
    }

    if (parent->type != VFS_FILE_DIRECTORY) {
        klog_error("ramfs_rmdir: Parent is not a directory");
        return -1;
    }

    parent_data = (ramfs_inode_t *)parent->fs_data;

    /* Find the entry */
    prev = NULL;
    entry = parent_data->entries;
    while (entry != NULL) {
        if (strcmp(entry->name, name) == 0) {
            /* Found it - check it's a directory */
            if (entry->inode->type != VFS_FILE_DIRECTORY) {
                klog_error("ramfs_rmdir: '%s' is not a directory", name);
                return -1;
            }

            /* Check if directory is empty */
            child_data = (ramfs_inode_t *)entry->inode->fs_data;
            if (child_data->num_entries > 0) {
                klog_error("ramfs_rmdir: Directory '%s' is not empty", name);
                return -1;
            }

            /* Remove from list */
            if (prev == NULL) {
                parent_data->entries = entry->next;
            } else {
                prev->next = entry->next;
            }
            parent_data->num_entries--;

            /* Free the inode */
            kfree(child_data);
            kfree(entry->inode);
            kfree(entry);

            klog_debug("ramfs_rmdir: Deleted directory '%s'", name);
            return 0;
        }
        prev = entry;
        entry = entry->next;
    }

    klog_error("ramfs_rmdir: Directory '%s' not found", name);
    return -1;
}

/**
 * Read from a file
 */
static ssize_t ramfs_file_read(vfs_file_t *file, void *buf, size_t count)
{
    ramfs_inode_t *ramfs_data;
    size_t bytes_to_read;

    if (file == NULL || buf == NULL || file->inode == NULL) {
        return -1;
    }

    ramfs_data = (ramfs_inode_t *)file->inode->fs_data;

    /* Check if offset is beyond file size */
    if (file->offset >= file->inode->size) {
        return 0;  /* EOF */
    }

    /* Calculate bytes to read */
    bytes_to_read = count;
    if (file->offset + bytes_to_read > file->inode->size) {
        bytes_to_read = file->inode->size - file->offset;
    }

    /* Copy data */
    if (ramfs_data->data != NULL) {
        memcpy(buf, ramfs_data->data + file->offset, bytes_to_read);
    }

    /* Update offset */
    file->offset += bytes_to_read;

    klog_debug("ramfs_read: %u bytes from offset %llu",
               (uint32_t)bytes_to_read, file->offset - bytes_to_read);

    return (ssize_t)bytes_to_read;
}

/**
 * Write to a file
 */
static ssize_t ramfs_file_write(vfs_file_t *file, const void *buf, size_t count)
{
    ramfs_inode_t *ramfs_data;
    size_t new_size;
    char *new_data;

    if (file == NULL || buf == NULL || file->inode == NULL) {
        return -1;
    }

    ramfs_data = (ramfs_inode_t *)file->inode->fs_data;

    /* Calculate new file size */
    new_size = file->offset + count;

    /* Check size limit */
    if (new_size > RAMFS_MAX_FILE_SIZE) {
        klog_error("ramfs_write: File too large (%u > %u)",
                   (uint32_t)new_size, RAMFS_MAX_FILE_SIZE);
        return -1;
    }

    /* Resize buffer if needed */
    if (new_size > ramfs_data->data_size) {
        new_data = (char *)kmalloc(new_size);
        if (new_data == NULL) {
            klog_error("ramfs_write: Failed to allocate buffer");
            return -1;
        }

        /* Copy existing data */
        if (ramfs_data->data != NULL) {
            memcpy(new_data, ramfs_data->data, file->inode->size);
            kfree(ramfs_data->data);
        }

        ramfs_data->data = new_data;
        ramfs_data->data_size = new_size;
    }

    /* Write data */
    memcpy(ramfs_data->data + file->offset, buf, count);

    /* Update file size and offset */
    if (new_size > file->inode->size) {
        file->inode->size = new_size;
    }
    file->offset += count;

    klog_debug("ramfs_write: %u bytes at offset %llu",
               (uint32_t)count, file->offset - count);

    return (ssize_t)count;
}

/**
 * Close a file
 */
static int ramfs_file_close(vfs_file_t *file)
{
#ifndef DEBUG_ENABLED
    (void)file;  /* Unused when debug logging is disabled */
#endif
    /* Nothing special to do for ramfs */
    klog_debug("ramfs_close: inode %llu", file->inode->ino);
    return 0;
}

/**
 * Read directory entries
 */
static int ramfs_dir_readdir(vfs_file_t *file, vfs_dirent_t **dirent)
{
    ramfs_inode_t *ramfs_data;
    ramfs_dirent_t *entry;
    static vfs_dirent_t vfs_entry;  /* Static buffer for return */
    size_t index;

    if (file == NULL || dirent == NULL || file->inode == NULL) {
        return -1;
    }

    if (file->inode->type != VFS_FILE_DIRECTORY) {
        return -1;
    }

    ramfs_data = (ramfs_inode_t *)file->inode->fs_data;

    /* Use offset as entry index */
    index = (size_t)file->offset;

    /* TODO: Add . and .. support - currently disabled for debugging */

    if (index >= ramfs_data->num_entries) {
        return -1;  /* No more entries */
    }

    /* Find entry at index */
    entry = ramfs_data->entries;
    while (index > 0 && entry != NULL) {
        entry = entry->next;
        index--;
    }

    if (entry == NULL) {
        klog_debug("ramfs_readdir: entry is NULL at index %zu", index);
        return -1;
    }

    /* Safety check: ensure entry has valid inode */
    if (entry->inode == NULL) {
        klog_error("ramfs_readdir: entry->inode is NULL for '%s'", entry->name);
        return -1;
    }

    /* Convert ramfs_dirent_t to vfs_dirent_t */
    vfs_entry.ino = entry->inode->ino;
    vfs_entry.type = entry->inode->type;
    vfs_entry.size = entry->inode->size;
    strncpy(vfs_entry.name, entry->name, MAX_FILENAME_LEN - 1);
    vfs_entry.name[MAX_FILENAME_LEN - 1] = '\0';
    vfs_entry.next = NULL;

    *dirent = &vfs_entry;
    file->offset++;

    klog_debug("ramfs_readdir: returning entry '%s' (ino=%llu, type=%d)",
               vfs_entry.name, vfs_entry.ino, vfs_entry.type);

    return 0;
}

/**
 * Create ramfs filesystem
 */
vfs_filesystem_t *ramfs_create(void)
{
    vfs_filesystem_t *fs;
    vfs_inode_t *root;

    klog_info("Creating ramfs filesystem...");

    /* Allocate filesystem structure */
    fs = (vfs_filesystem_t *)kmalloc(sizeof(vfs_filesystem_t));
    if (fs == NULL) {
        klog_error("Failed to allocate ramfs structure");
        return NULL;
    }

    /* Initialize filesystem */
    strcpy(fs->name, "ramfs");
    fs->ops = &ramfs_ops;
    fs->fs_data = NULL;
    fs->next = NULL;

    /* Create root directory inode */
    root = ramfs_create_inode(VFS_FILE_DIRECTORY, 0755);
    if (root == NULL) {
        klog_error("Failed to create root inode");
        kfree(fs);
        return NULL;
    }

    root->fs = fs;
    fs->root = root;

    klog_info("Ramfs created with root inode %llu", root->ino);
    return fs;
}

/**
 * Destroy ramfs filesystem
 */
void ramfs_destroy(vfs_filesystem_t *fs)
{
    /* TODO: Free all inodes and data */
    if (fs != NULL) {
        kfree(fs);
    }
}

/* ============================================================================
 * End of ramfs.c
 * ============================================================================ */
