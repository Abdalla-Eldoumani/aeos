/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/fs/vfs.c
 * Description: Virtual File System implementation
 * ============================================================================ */

#include <aeos/vfs.h>
#include <aeos/process.h>
#include <aeos/heap.h>
#include <aeos/kprintf.h>
#include <aeos/string.h>

/* Global VFS state */
static struct {
    vfs_filesystem_t *filesystems;  /* Registered filesystems */
    vfs_mount_t *mounts;            /* Mount points */
    vfs_inode_t *root;              /* Root inode (/) */
    bool initialized;
} vfs;

/* ============================================================================
 * VFS Initialization
 * ============================================================================ */

void vfs_init(void)
{
    klog_info("Initializing Virtual File System...");

    vfs.filesystems = NULL;
    vfs.mounts = NULL;
    vfs.root = NULL;
    vfs.initialized = true;

    klog_info("VFS initialized");
}

/* ============================================================================
 * Filesystem Registration
 * ============================================================================ */

int vfs_register_filesystem(vfs_filesystem_t *fs)
{
    if (!vfs.initialized || fs == NULL) {
        return -1;
    }

    klog_info("Registering filesystem: %s", fs->name);

    /* Add to linked list of filesystems */
    fs->next = vfs.filesystems;
    vfs.filesystems = fs;

    return 0;
}

/* ============================================================================
 * Mount/Unmount
 * ============================================================================ */

int vfs_mount(const char *path, vfs_filesystem_t *fs)
{
    vfs_mount_t *mount;

    if (!vfs.initialized || path == NULL || fs == NULL) {
        return -1;
    }

    klog_info("Mounting %s at %s", fs->name, path);

    /* Allocate mount structure */
    mount = (vfs_mount_t *)kmalloc(sizeof(vfs_mount_t));
    if (mount == NULL) {
        klog_error("Failed to allocate mount structure");
        return -1;
    }

    /* Copy path */
    strncpy(mount->path, path, MAX_PATH_LEN - 1);
    mount->path[MAX_PATH_LEN - 1] = '\0';

    mount->fs = fs;
    mount->mountpoint = NULL;  /* TODO: lookup mountpoint inode */

    /* Special case: mounting root filesystem */
    if (strcmp(path, "/") == 0) {
        vfs.root = fs->root;
        klog_info("Root filesystem mounted");
    }

    /* Add to mount list */
    mount->next = vfs.mounts;
    vfs.mounts = mount;

    return 0;
}

int vfs_unmount(const char *path)
{
    vfs_mount_t *mount, *prev;

    if (!vfs.initialized || path == NULL) {
        return -1;
    }

    prev = NULL;
    for (mount = vfs.mounts; mount != NULL; mount = mount->next) {
        if (strcmp(mount->path, path) == 0) {
            /* Found it */
            if (prev == NULL) {
                vfs.mounts = mount->next;
            } else {
                prev->next = mount->next;
            }

            kfree(mount);
            klog_info("Unmounted %s", path);
            return 0;
        }
        prev = mount;
    }

    klog_error("Mount point not found: %s", path);
    return -1;
}

/* ============================================================================
 * File Descriptor Table Operations
 * ============================================================================ */

vfs_fd_table_t *vfs_fd_table_create(void)
{
    vfs_fd_table_t *table;
    int i;

    table = (vfs_fd_table_t *)kmalloc(sizeof(vfs_fd_table_t));
    if (table == NULL) {
        return NULL;
    }

    /* Initialize all fds as unused */
    for (i = 0; i < MAX_OPEN_FILES; i++) {
        table->fds[i].file = NULL;
        table->fds[i].flags = 0;
    }

    table->next_fd = 0;

    klog_debug("Created file descriptor table");
    return table;
}

void vfs_fd_table_destroy(vfs_fd_table_t *table)
{
    int i;

    if (table == NULL) {
        return;
    }

    /* Close all open files */
    for (i = 0; i < MAX_OPEN_FILES; i++) {
        if (table->fds[i].file != NULL) {
            vfs_close(i);
        }
    }

    kfree(table);
    klog_debug("Destroyed file descriptor table");
}

int vfs_fd_alloc(vfs_file_t *file, uint32_t flags)
{
    process_t *proc;
    vfs_fd_table_t *table;
    int fd;

    proc = process_current();
    if (proc == NULL || proc->fd_table == NULL) {
        klog_error("No current process or fd table");
        return -1;
    }

    table = proc->fd_table;

    /* Find free fd */
    for (fd = 0; fd < MAX_OPEN_FILES; fd++) {
        if (table->fds[fd].file == NULL) {
            table->fds[fd].file = file;
            table->fds[fd].flags = flags;
            file->refcount++;
            klog_debug("Allocated fd %d", fd);
            return fd;
        }
    }

    klog_error("No free file descriptors");
    return -1;
}

vfs_file_t *vfs_fd_to_file(int fd)
{
    process_t *proc;
    vfs_fd_table_t *table;

    proc = process_current();
    if (proc == NULL || proc->fd_table == NULL) {
        return NULL;
    }

    table = proc->fd_table;

    if (fd < 0 || fd >= MAX_OPEN_FILES) {
        return NULL;
    }

    return table->fds[fd].file;
}

void vfs_fd_free(int fd)
{
    process_t *proc;
    vfs_fd_table_t *table;
    vfs_file_t *file;

    proc = process_current();
    if (proc == NULL || proc->fd_table == NULL) {
        return;
    }

    table = proc->fd_table;

    if (fd < 0 || fd >= MAX_OPEN_FILES) {
        return;
    }

    file = table->fds[fd].file;
    if (file != NULL) {
        file->refcount--;

        /* If no more references, close the file */
        if (file->refcount == 0) {
            if (file->inode && file->inode->fs && file->inode->fs->ops->file_close) {
                file->inode->fs->ops->file_close(file);
            }
            kfree(file);
        }
    }

    table->fds[fd].file = NULL;
    table->fds[fd].flags = 0;

    klog_debug("Freed fd %d", fd);
}

/* ============================================================================
 * Path Resolution
 * ============================================================================ */

/**
 * Split path into components (helper function)
 * Example: "/foo/bar/baz" -> ["foo", "bar", "baz"]
 */
static int split_path(const char *path, char **components, int max_components)
{
    char *path_copy;
    char *token;
    char *saveptr;
    int count = 0;

    if (path == NULL || components == NULL) {
        return -1;
    }

    /* Make a copy since strtok modifies the string */
    path_copy = kmalloc(strlen(path) + 1);
    if (path_copy == NULL) {
        return -1;
    }
    strcpy(path_copy, path);

    /* Tokenize by '/' */
    token = strtok_r(path_copy, "/", &saveptr);
    while (token != NULL && count < max_components) {
        components[count] = kmalloc(strlen(token) + 1);
        if (components[count] == NULL) {
            /* Cleanup on error */
            int i;
            for (i = 0; i < count; i++) {
                kfree(components[i]);
            }
            kfree(path_copy);
            return -1;
        }
        strcpy(components[count], token);
        count++;
        token = strtok_r(NULL, "/", &saveptr);
    }

    kfree(path_copy);
    return count;
}

int vfs_path_lookup(const char *path, vfs_inode_t **result)
{
    char *components[32];  /* Max path depth */
    int num_components;
    vfs_inode_t *current;
    int i;
    int ret;

    if (!vfs.initialized || path == NULL || result == NULL) {
        return -1;
    }

    /* Start at root */
    current = vfs.root;
    if (current == NULL) {
        klog_error("No root filesystem mounted");
        return -1;
    }

    /* Handle root path specially */
    if (strcmp(path, "/") == 0) {
        *result = current;
        return 0;
    }

    /* Split path into components */
    num_components = split_path(path, components, 32);
    if (num_components < 0) {
        return -1;
    }

    /* Walk the path */
    for (i = 0; i < num_components; i++) {
        /* Check if current is a directory */
        if (current->type != VFS_FILE_DIRECTORY) {
            klog_error("Not a directory: component %d", i);
            ret = -1;
            goto cleanup;
        }

        /* Lookup next component */
        if (current->fs == NULL || current->fs->ops == NULL ||
            current->fs->ops->inode_lookup == NULL) {
            klog_error("Filesystem doesn't support lookup");
            ret = -1;
            goto cleanup;
        }

        ret = current->fs->ops->inode_lookup(current, components[i], &current);
        if (ret < 0) {
            klog_error("Component not found: %s", components[i]);
            goto cleanup;
        }
    }

    *result = current;
    ret = 0;

cleanup:
    /* Free component strings */
    for (i = 0; i < num_components; i++) {
        kfree(components[i]);
    }

    return ret;
}

/* ============================================================================
 * File Operations
 * ============================================================================ */

int vfs_open(const char *path, uint32_t flags, uint32_t mode)
{
    vfs_inode_t *inode;
    vfs_file_t *file;
    int fd;
    int ret;

    if (!vfs.initialized || path == NULL) {
        return -1;
    }

    klog_debug("vfs_open: %s (flags=0x%x, mode=0x%x)", path, flags, mode);

    /* Lookup the file */
    ret = vfs_path_lookup(path, &inode);
    if (ret < 0) {
        /* File doesn't exist */
        if (flags & O_CREAT) {
            /* Create the file */
            ret = vfs_create(path, mode);
            if (ret < 0) {
                klog_error("Failed to create file: %s", path);
                return -1;
            }
            /* Now lookup the newly created file */
            ret = vfs_path_lookup(path, &inode);
            if (ret < 0) {
                klog_error("Failed to lookup created file: %s", path);
                return -1;
            }
        } else {
            klog_error("File not found: %s", path);
            return -1;
        }
    }

    /* Allocate file structure */
    file = (vfs_file_t *)kmalloc(sizeof(vfs_file_t));
    if (file == NULL) {
        klog_error("Failed to allocate file structure");
        return -1;
    }

    file->inode = inode;
    file->flags = flags;
    file->offset = 0;
    file->refcount = 0;  /* Will be incremented by fd_alloc */
    file->private_data = NULL;

    /* Allocate file descriptor */
    fd = vfs_fd_alloc(file, 0);
    if (fd < 0) {
        kfree(file);
        return -1;
    }

    klog_debug("Opened %s as fd %d", path, fd);
    return fd;
}

int vfs_close(int fd)
{
    klog_debug("vfs_close: fd=%d", fd);

    if (fd < 0 || fd >= MAX_OPEN_FILES) {
        return -1;
    }

    vfs_fd_free(fd);
    return 0;
}

ssize_t vfs_read(int fd, void *buf, size_t count)
{
    vfs_file_t *file;
    ssize_t ret;

    file = vfs_fd_to_file(fd);
    if (file == NULL) {
        klog_error("Invalid fd: %d", fd);
        return -1;
    }

    /* Check if file system supports read */
    if (file->inode == NULL || file->inode->fs == NULL ||
        file->inode->fs->ops == NULL || file->inode->fs->ops->file_read == NULL) {
        klog_error("Filesystem doesn't support read");
        return -1;
    }

    /* Call filesystem-specific read */
    ret = file->inode->fs->ops->file_read(file, buf, count);

    klog_debug("vfs_read: fd=%d, count=%u, ret=%d", fd, (uint32_t)count, (int)ret);
    return ret;
}

ssize_t vfs_write(int fd, const void *buf, size_t count)
{
    vfs_file_t *file;
    ssize_t ret;

    file = vfs_fd_to_file(fd);
    if (file == NULL) {
        klog_error("Invalid fd: %d", fd);
        return -1;
    }

    /* Check if filesystem supports write */
    if (file->inode == NULL || file->inode->fs == NULL ||
        file->inode->fs->ops == NULL || file->inode->fs->ops->file_write == NULL) {
        klog_error("Filesystem doesn't support write");
        return -1;
    }

    /* Call filesystem-specific write */
    ret = file->inode->fs->ops->file_write(file, buf, count);

    klog_debug("vfs_write: fd=%d, count=%u, ret=%d", fd, (uint32_t)count, (int)ret);
    return ret;
}

int64_t vfs_seek(int fd, int64_t offset, int whence)
{
    vfs_file_t *file;
    int64_t new_offset;

    file = vfs_fd_to_file(fd);
    if (file == NULL) {
        return -1;
    }

    switch (whence) {
    case SEEK_SET:
        new_offset = offset;
        break;
    case SEEK_CUR:
        new_offset = file->offset + offset;
        break;
    case SEEK_END:
        new_offset = file->inode->size + offset;
        break;
    default:
        return -1;
    }

    /* Check bounds */
    if (new_offset < 0) {
        return -1;
    }

    file->offset = new_offset;

    klog_debug("vfs_seek: fd=%d, offset=%lld, whence=%d -> %lld",
               fd, offset, whence, new_offset);

    return new_offset;
}

int vfs_readdir(int fd, vfs_dirent_t *dirent)
{
    vfs_file_t *file;
    vfs_dirent_t *result;
    int ret;

    file = vfs_fd_to_file(fd);
    if (file == NULL || dirent == NULL) {
        return -1;
    }

    /* Check if this is a directory */
    if (file->inode->type != VFS_FILE_DIRECTORY) {
        return -1;
    }

    /* Check if filesystem supports readdir */
    if (file->inode->fs == NULL || file->inode->fs->ops == NULL ||
        file->inode->fs->ops->dir_readdir == NULL) {
        return -1;
    }

    /* Call filesystem-specific readdir */
    ret = file->inode->fs->ops->dir_readdir(file, &result);
    if (ret < 0 || result == NULL) {
        return -1;
    }

    klog_debug("vfs_readdir: about to copy result to dirent");
    klog_debug("vfs_readdir: result=%p, dirent=%p", result, dirent);
    klog_debug("vfs_readdir: result->name='%s', ino=%llu", result->name, result->ino);

    /* Copy to user-provided buffer - manual copy to avoid memcpy optimization issues */
    dirent->ino = result->ino;
    dirent->type = result->type;
    dirent->size = result->size;
    strncpy(dirent->name, result->name, MAX_FILENAME_LEN - 1);
    dirent->name[MAX_FILENAME_LEN - 1] = '\0';
    dirent->next = result->next;

    klog_debug("vfs_readdir: copy complete");

    return 0;
}

/* ============================================================================
 * File/Directory Creation/Deletion
 * ============================================================================ */

int vfs_create(const char *path, uint32_t mode)
{
    char *components[32];
    char *filename;
    char *dir_path;
    vfs_inode_t *parent_inode;
    vfs_inode_t *new_inode;
    int num_components;
    int ret;
    int i;

    if (!vfs.initialized || path == NULL) {
        return -1;
    }

    klog_debug("vfs_create: %s (mode=0x%x)", path, mode);

    /* Split path to get parent directory and filename */
    num_components = split_path(path, components, 32);
    if (num_components <= 0) {
        /* Creating file in root */
        filename = (char *)path;
        parent_inode = vfs.root;
    } else {
        /* Last component is the filename */
        filename = components[num_components - 1];

        if (num_components == 1) {
            /* Creating in root directory */
            parent_inode = vfs.root;
        } else {
            /* Build parent directory path */
            size_t path_len = 0;
            for (i = 0; i < num_components - 1; i++) {
                path_len += strlen(components[i]) + 1; /* +1 for '/' */
            }
            dir_path = (char *)kmalloc(path_len + 2);
            if (dir_path == NULL) {
                ret = -1;
                goto cleanup;
            }

            strcpy(dir_path, "/");
            for (i = 0; i < num_components - 1; i++) {
                strcat(dir_path, components[i]);
                if (i < num_components - 2) {
                    strcat(dir_path, "/");
                }
            }

            /* Lookup parent directory */
            ret = vfs_path_lookup(dir_path, &parent_inode);
            kfree(dir_path);
            if (ret < 0) {
                klog_error("Parent directory not found");
                goto cleanup;
            }
        }
    }

    /* Check if parent supports file creation */
    if (parent_inode->fs == NULL || parent_inode->fs->ops == NULL ||
        parent_inode->fs->ops->inode_create == NULL) {
        klog_error("Filesystem doesn't support file creation");
        ret = -1;
        goto cleanup;
    }

    /* Create the file */
    ret = parent_inode->fs->ops->inode_create(parent_inode, filename, mode, &new_inode);
    if (ret < 0) {
        klog_error("Failed to create file: %s", filename);
        goto cleanup;
    }

    klog_debug("Created file: %s", path);
    ret = 0;

cleanup:
    if (num_components > 0) {
        for (i = 0; i < num_components; i++) {
            kfree(components[i]);
        }
    }
    return ret;
}

int vfs_mkdir(const char *path, uint32_t mode)
{
    char *components[32];
    char *dirname;
    char *parent_path;
    vfs_inode_t *parent_inode;
    vfs_inode_t *new_inode;
    int num_components;
    int ret;
    int i;

    if (!vfs.initialized || path == NULL) {
        return -1;
    }

    klog_debug("vfs_mkdir: %s (mode=0x%x)", path, mode);

    /* Split path to get parent directory and dirname */
    num_components = split_path(path, components, 32);
    if (num_components <= 0) {
        /* Can't create root */
        klog_error("Invalid path for mkdir");
        return -1;
    }

    /* Last component is the new directory name */
    dirname = components[num_components - 1];

    if (num_components == 1) {
        /* Creating in root directory */
        parent_inode = vfs.root;
    } else {
        /* Build parent directory path */
        size_t path_len = 0;
        for (i = 0; i < num_components - 1; i++) {
            path_len += strlen(components[i]) + 1;
        }
        parent_path = (char *)kmalloc(path_len + 2);
        if (parent_path == NULL) {
            ret = -1;
            goto cleanup;
        }

        strcpy(parent_path, "/");
        for (i = 0; i < num_components - 1; i++) {
            strcat(parent_path, components[i]);
            if (i < num_components - 2) {
                strcat(parent_path, "/");
            }
        }

        /* Lookup parent directory */
        ret = vfs_path_lookup(parent_path, &parent_inode);
        kfree(parent_path);
        if (ret < 0) {
            klog_error("Parent directory not found");
            goto cleanup;
        }
    }

    /* Check if parent supports directory creation */
    if (parent_inode->fs == NULL || parent_inode->fs->ops == NULL ||
        parent_inode->fs->ops->inode_mkdir == NULL) {
        klog_error("Filesystem doesn't support directory creation");
        ret = -1;
        goto cleanup;
    }

    /* Create the directory */
    ret = parent_inode->fs->ops->inode_mkdir(parent_inode, dirname, mode, &new_inode);
    if (ret < 0) {
        klog_error("Failed to create directory: %s", dirname);
        goto cleanup;
    }

    klog_debug("Created directory: %s", path);
    ret = 0;

cleanup:
    for (i = 0; i < num_components; i++) {
        kfree(components[i]);
    }
    return ret;
}

int vfs_unlink(const char *path)
{
    char *components[32];
    char *filename;
    char *parent_path;
    vfs_inode_t *parent_inode;
    int num_components;
    int ret;
    int i;

    if (!vfs.initialized || path == NULL) {
        return -1;
    }

    klog_debug("vfs_unlink: %s", path);

    /* Split path to get parent directory and filename */
    num_components = split_path(path, components, 32);
    if (num_components <= 0) {
        klog_error("Invalid path for unlink");
        return -1;
    }

    /* Last component is the filename */
    filename = components[num_components - 1];

    if (num_components == 1) {
        /* Deleting from root directory */
        parent_inode = vfs.root;
    } else {
        /* Build parent directory path */
        size_t path_len = 0;
        for (i = 0; i < num_components - 1; i++) {
            path_len += strlen(components[i]) + 1;
        }
        parent_path = (char *)kmalloc(path_len + 2);
        if (parent_path == NULL) {
            ret = -1;
            goto cleanup;
        }

        strcpy(parent_path, "/");
        for (i = 0; i < num_components - 1; i++) {
            strcat(parent_path, components[i]);
            if (i < num_components - 2) {
                strcat(parent_path, "/");
            }
        }

        /* Lookup parent directory */
        ret = vfs_path_lookup(parent_path, &parent_inode);
        kfree(parent_path);
        if (ret < 0) {
            klog_error("Parent directory not found");
            goto cleanup;
        }
    }

    /* Check if parent supports file deletion */
    if (parent_inode->fs == NULL || parent_inode->fs->ops == NULL ||
        parent_inode->fs->ops->inode_unlink == NULL) {
        klog_error("Filesystem doesn't support file deletion");
        ret = -1;
        goto cleanup;
    }

    /* Delete the file */
    ret = parent_inode->fs->ops->inode_unlink(parent_inode, filename);
    if (ret < 0) {
        klog_error("Failed to delete file: %s", filename);
        goto cleanup;
    }

    klog_debug("Deleted file: %s", path);
    ret = 0;

cleanup:
    for (i = 0; i < num_components; i++) {
        kfree(components[i]);
    }
    return ret;
}

int vfs_rmdir(const char *path)
{
    char *components[32];
    char *dirname;
    char *parent_path;
    vfs_inode_t *parent_inode;
    int num_components;
    int ret;
    int i;

    if (!vfs.initialized || path == NULL) {
        return -1;
    }

    klog_debug("vfs_rmdir: %s", path);

    /* Split path to get parent directory and dirname */
    num_components = split_path(path, components, 32);
    if (num_components <= 0) {
        klog_error("Invalid path for rmdir");
        return -1;
    }

    /* Last component is the directory name */
    dirname = components[num_components - 1];

    if (num_components == 1) {
        /* Deleting from root directory */
        parent_inode = vfs.root;
    } else {
        /* Build parent directory path */
        size_t path_len = 0;
        for (i = 0; i < num_components - 1; i++) {
            path_len += strlen(components[i]) + 1;
        }
        parent_path = (char *)kmalloc(path_len + 2);
        if (parent_path == NULL) {
            ret = -1;
            goto cleanup;
        }

        strcpy(parent_path, "/");
        for (i = 0; i < num_components - 1; i++) {
            strcat(parent_path, components[i]);
            if (i < num_components - 2) {
                strcat(parent_path, "/");
            }
        }

        /* Lookup parent directory */
        ret = vfs_path_lookup(parent_path, &parent_inode);
        kfree(parent_path);
        if (ret < 0) {
            klog_error("Parent directory not found");
            goto cleanup;
        }
    }

    /* Check if parent supports directory deletion */
    if (parent_inode->fs == NULL || parent_inode->fs->ops == NULL ||
        parent_inode->fs->ops->inode_rmdir == NULL) {
        klog_error("Filesystem doesn't support directory deletion");
        ret = -1;
        goto cleanup;
    }

    /* Delete the directory */
    ret = parent_inode->fs->ops->inode_rmdir(parent_inode, dirname);
    if (ret < 0) {
        klog_error("Failed to delete directory: %s", dirname);
        goto cleanup;
    }

    klog_debug("Deleted directory: %s", path);
    ret = 0;

cleanup:
    for (i = 0; i < num_components; i++) {
        kfree(components[i]);
    }
    return ret;
}

/**
 * Get the root filesystem
 */
vfs_filesystem_t *vfs_get_root_fs(void)
{
    if (vfs.root != NULL) {
        return vfs.root->fs;
    }
    return NULL;
}

/* ============================================================================
 * End of vfs.c
 * ============================================================================ */
