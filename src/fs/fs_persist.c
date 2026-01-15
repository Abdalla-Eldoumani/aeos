/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/fs/fs_persist.c
 * Description: Filesystem persistence implementation
 * ============================================================================ */

#include <aeos/fs_persist.h>
#include <aeos/vfs.h>
#include <aeos/ramfs.h>
#include <aeos/heap.h>
#include <aeos/kprintf.h>
#include <aeos/string.h>
#include <aeos/pflash.h>
#include <aeos/semihosting.h>

/* Default filename for persistent storage on host */
#define FS_IMAGE_FILENAME "aeos_fs.img"

/* Persistent storage buffer (allocated at fixed address) */
static char fs_storage[FS_IMAGE_MAX_SIZE] __attribute__((aligned(4096)));

/**
 * Recursively serialize an inode and its children
 */
static ssize_t serialize_inode(vfs_inode_t *inode, void *buffer, size_t buffer_size,
                                size_t *offset, uint64_t parent_ino, const char *name)
{
    fs_inode_entry_t entry;
    ramfs_inode_t *ramfs_data;
    ramfs_dirent_t *child;
    size_t entry_offset;
    size_t data_offset;

    klog_debug("serialize_inode: inode=%p name='%s' offset=%u", (void *)inode, name, (uint32_t)*offset);

    if (*offset + sizeof(fs_inode_entry_t) > buffer_size) {
        klog_error("Buffer too small for inode entry");
        return -1;
    }

    /* Prepare inode entry */
    klog_debug("serialize_inode: preparing entry");
    entry.ino = inode->ino;
    klog_debug("serialize_inode: set ino=%llu", entry.ino);
    entry.type = inode->type;
    klog_debug("serialize_inode: set type=%u", entry.type);
    entry.mode = inode->mode;
    klog_debug("serialize_inode: set mode=%u", entry.mode);
    entry.size = inode->size;
    klog_debug("serialize_inode: set size=%llu", entry.size);
    entry.parent_ino = parent_ino;
    klog_debug("serialize_inode: set parent_ino=%llu", parent_ino);

    /* Manual string copy to avoid potential strncpy issues */
    klog_debug("serialize_inode: copying name '%s'", name);
    {
        size_t i;
        for (i = 0; i < sizeof(entry.name) - 1 && name[i] != '\0'; i++) {
            entry.name[i] = name[i];
        }
        entry.name[i] = '\0';
    }
    klog_debug("serialize_inode: name copied");

    entry.data_offset = 0;
    entry.num_children = 0;
    klog_debug("serialize_inode: entry prepared");

    klog_debug("serialize_inode: getting fs_data");
    ramfs_data = (ramfs_inode_t *)inode->fs_data;
    klog_debug("serialize_inode: ramfs_data=%p", (void *)ramfs_data);

    /* If it's a file with data, save data offset */
    if (inode->type == VFS_FILE_REGULAR && ramfs_data->data != NULL && inode->size > 0) {
        klog_debug("serialize_inode: file with data, size=%llu", inode->size);
        /* Reserve space for entry first */
        entry_offset = *offset;
        *offset += sizeof(fs_inode_entry_t);

        /* Write file data */
        data_offset = *offset;
        if (*offset + inode->size > buffer_size) {
            klog_error("Buffer too small for file data");
            return -1;
        }

        klog_debug("serialize_inode: copying file data");
        memcpy((char *)buffer + *offset, ramfs_data->data, inode->size);
        *offset += inode->size;

        entry.data_offset = (uint32_t)data_offset;

        /* Write entry */
        klog_debug("serialize_inode: writing file entry");
        memcpy((char *)buffer + entry_offset, &entry, sizeof(entry));
    } else if (inode->type == VFS_FILE_DIRECTORY) {
        klog_debug("serialize_inode: directory with %u children", (uint32_t)ramfs_data->num_entries);
        /* For directories, count children */
        entry.num_children = (uint32_t)ramfs_data->num_entries;

        /* Write entry */
        entry_offset = *offset;
        klog_debug("serialize_inode: writing directory entry");
        memcpy((char *)buffer + *offset, &entry, sizeof(entry));
        *offset += sizeof(fs_inode_entry_t);

        /* Recursively serialize children */
        klog_debug("serialize_inode: serializing children");
        child = ramfs_data->entries;
        while (child != NULL) {
            klog_debug("serialize_inode: serializing child '%s'", child->name);
            ssize_t ret = serialize_inode(child->inode, buffer, buffer_size, offset,
                                          inode->ino, child->name);
            if (ret < 0) {
                return ret;
            }
            child = child->next;
        }
    } else {
        /* Empty file or other type */
        klog_debug("serialize_inode: empty file or other type");
        memcpy((char *)buffer + *offset, &entry, sizeof(entry));
        *offset += sizeof(fs_inode_entry_t);
    }

    klog_debug("serialize_inode: done for '%s'", name);
    return 0;
}

/**
 * Save filesystem to memory buffer
 */
ssize_t fs_save(vfs_filesystem_t *fs, void *buffer, size_t buffer_size)
{
    fs_image_header_t header;
    size_t offset;

    if (fs == NULL || buffer == NULL || buffer_size < sizeof(header)) {
        klog_error("Invalid parameters for fs_save");
        return -1;
    }

    klog_info("Saving filesystem...");

    /* Prepare header */
    header.magic = FS_MAGIC;
    header.version = FS_VERSION;
    header.timestamp = 0;  /* TODO: Get real timestamp when RTC is implemented */
    header.num_inodes = 0;  /* Will be calculated during serialization */
    header.data_size = 0;

    /* Write header */
    offset = 0;
    klog_debug("Writing header at offset %u", (uint32_t)offset);
    memcpy((char *)buffer + offset, &header, sizeof(header));
    offset += sizeof(header);
    klog_debug("Header written, offset now %u", (uint32_t)offset);

    /* Serialize filesystem tree starting from root */
    if (fs->root != NULL) {
        klog_debug("Serializing root inode at %p", (void *)fs->root);
        ssize_t ret = serialize_inode(fs->root, buffer, buffer_size, &offset, 0, "/");
        if (ret < 0) {
            klog_error("Failed to serialize filesystem");
            return ret;
        }
    }

    /* Update header with final size */
    header.data_size = (uint32_t)(offset - sizeof(header));
    memcpy(buffer, &header, sizeof(header));

    klog_info("Filesystem saved (%u bytes)", (uint32_t)offset);
    return (ssize_t)offset;
}

/**
 * Recursively deserialize inodes
 */
static int deserialize_inodes(vfs_filesystem_t *fs, const void *buffer, size_t buffer_size,
                               size_t *offset, vfs_inode_t *parent, uint32_t num_entries)
{
    uint32_t i;
    fs_inode_entry_t entry;
    vfs_inode_t *inode;
    ramfs_inode_t *ramfs_data;
    ramfs_dirent_t *dir_entry;
    ramfs_inode_t *parent_data;

    for (i = 0; i < num_entries; i++) {
        /* Read entry */
        if (*offset + sizeof(entry) > buffer_size) {
            klog_error("Buffer underrun reading inode entry");
            return -1;
        }

        memcpy(&entry, (const char *)buffer + *offset, sizeof(entry));
        *offset += sizeof(entry);

        /* Create inode */
        inode = (vfs_inode_t *)kmalloc(sizeof(vfs_inode_t));
        if (inode == NULL) {
            klog_error("Failed to allocate inode");
            return -1;
        }

        ramfs_data = (ramfs_inode_t *)kmalloc(sizeof(ramfs_inode_t));
        if (ramfs_data == NULL) {
            kfree(inode);
            klog_error("Failed to allocate ramfs data");
            return -1;
        }

        /* Initialize inode */
        inode->ino = entry.ino;
        inode->type = (vfs_file_type_t)entry.type;
        inode->mode = entry.mode;
        inode->size = entry.size;
        inode->blocks = 0;
        inode->atime = 0;
        inode->mtime = 0;
        inode->ctime = 0;
        inode->nlinks = 1;
        inode->uid = 0;
        inode->gid = 0;
        inode->fs_data = ramfs_data;
        inode->fs = fs;

        /* Initialize ramfs data */
        ramfs_data->data = NULL;
        ramfs_data->data_size = 0;
        ramfs_data->entries = NULL;
        ramfs_data->num_entries = 0;

        /* Load file data if present */
        if (inode->type == VFS_FILE_REGULAR && entry.data_offset > 0 && entry.size > 0) {
            ramfs_data->data = (char *)kmalloc(entry.size);
            if (ramfs_data->data == NULL) {
                kfree(ramfs_data);
                kfree(inode);
                klog_error("Failed to allocate file data");
                return -1;
            }

            if (entry.data_offset + entry.size > buffer_size) {
                kfree(ramfs_data->data);
                kfree(ramfs_data);
                kfree(inode);
                klog_error("Invalid data offset");
                return -1;
            }

            memcpy(ramfs_data->data, (const char *)buffer + entry.data_offset, entry.size);
            ramfs_data->data_size = entry.size;
        }

        /* Add to parent directory if not root */
        if (parent != NULL) {
            parent_data = (ramfs_inode_t *)parent->fs_data;

            dir_entry = (ramfs_dirent_t *)kmalloc(sizeof(ramfs_dirent_t));
            if (dir_entry == NULL) {
                if (ramfs_data->data != NULL) {
                    kfree(ramfs_data->data);
                }
                kfree(ramfs_data);
                kfree(inode);
                klog_error("Failed to allocate directory entry");
                return -1;
            }

            strncpy(dir_entry->name, entry.name, sizeof(dir_entry->name) - 1);
            dir_entry->name[sizeof(dir_entry->name) - 1] = '\0';
            dir_entry->inode = inode;
            dir_entry->next = parent_data->entries;

            parent_data->entries = dir_entry;
            parent_data->num_entries++;
        } else {
            /* This is the root inode */
            fs->root = inode;
        }

        klog_debug("Loaded inode %llu: %s (type=%d, size=%llu)",
                   inode->ino, entry.name, inode->type, inode->size);

        /* Recursively load children if directory */
        if (inode->type == VFS_FILE_DIRECTORY && entry.num_children > 0) {
            int ret = deserialize_inodes(fs, buffer, buffer_size, offset,
                                         inode, entry.num_children);
            if (ret < 0) {
                return ret;
            }
        }
    }

    return 0;
}

/**
 * Load filesystem from memory buffer
 */
int fs_load(vfs_filesystem_t *fs, const void *buffer, size_t buffer_size)
{
    fs_image_header_t header;
    size_t offset;

    if (fs == NULL || buffer == NULL || buffer_size < sizeof(header)) {
        klog_error("Invalid parameters for fs_load");
        return -1;
    }

    /* Read header */
    memcpy(&header, buffer, sizeof(header));

    /* Validate header */
    if (header.magic != FS_MAGIC) {
        klog_debug("Invalid filesystem magic (expected 0x%x, got 0x%x)",
                   FS_MAGIC, header.magic);
        return -1;
    }

    if (header.version != FS_VERSION) {
        klog_warn("Filesystem version mismatch (expected %u, got %u)",
                  FS_VERSION, header.version);
        return -1;
    }

    klog_info("Loading filesystem (version %u, %u bytes)...",
              header.version, header.data_size);

    offset = sizeof(header);

    /* Deserialize the root inode and its children */
    int ret = deserialize_inodes(fs, buffer, buffer_size, &offset, NULL, 1);
    if (ret < 0) {
        klog_error("Failed to deserialize filesystem");
        return ret;
    }

    klog_info("Filesystem loaded successfully");
    return 0;
}

/**
 * Get persistent storage buffer
 */
void *fs_get_storage_buffer(void)
{
    return fs_storage;
}

/**
 * Get persistent storage size
 */
size_t fs_get_storage_size(void)
{
    return FS_IMAGE_MAX_SIZE;
}

/**
 * Save filesystem to disk via semihosting
 * Writes filesystem image to host file system
 */
int fs_save_to_disk(vfs_filesystem_t *fs)
{
    void *buffer;
    size_t buffer_size;
    ssize_t bytes_saved;
    int host_fd;
    size_t not_written;

    klog_info("Saving filesystem via semihosting...");

    /* Check if semihosting is available */
    if (!semihost_available()) {
        klog_warn("Semihosting not available - cannot save to host");
        klog_info("Start QEMU with: -semihosting-config enable=on,target=native");
        return -1;
    }

    /* Get storage buffer */
    buffer = fs_get_storage_buffer();
    buffer_size = fs_get_storage_size();

    /* Save to memory buffer first */
    bytes_saved = fs_save(fs, buffer, buffer_size);
    if (bytes_saved < 0) {
        klog_error("Failed to save filesystem to buffer");
        return -1;
    }

    klog_debug("Writing %d bytes to host file '%s'", (int)bytes_saved, FS_IMAGE_FILENAME);

    /* Open host file for writing (create/truncate) */
    host_fd = semihost_open(FS_IMAGE_FILENAME, SEMIHOST_OPEN_WB);
    if (host_fd < 0) {
        klog_error("Failed to open host file for writing");
        return -1;
    }

    /* Write data to host file */
    not_written = semihost_write(host_fd, buffer, (size_t)bytes_saved);
    if (not_written != 0) {
        klog_error("Failed to write to host file (%u bytes not written)", (uint32_t)not_written);
        semihost_close(host_fd);
        return -1;
    }

    /* Close file */
    semihost_close(host_fd);

    klog_info("Filesystem saved to '%s' (%d bytes)", FS_IMAGE_FILENAME, (int)bytes_saved);
    return 0;
}

/**
 * Load filesystem from disk via semihosting
 * Reads filesystem image from host file system
 */
int fs_load_from_disk(vfs_filesystem_t *fs)
{
    void *buffer;
    size_t buffer_size;
    int ret;
    int host_fd;
    ssize_t file_len;
    size_t not_read;
    fs_image_header_t *header;

    klog_info("Loading filesystem via semihosting...");

    /* Check if semihosting is available */
    if (!semihost_available()) {
        klog_info("Semihosting not available - starting with fresh filesystem");
        return -1;
    }

    /* Get storage buffer */
    buffer = fs_get_storage_buffer();
    buffer_size = fs_get_storage_size();

    /* Try to open host file for reading */
    host_fd = semihost_open(FS_IMAGE_FILENAME, SEMIHOST_OPEN_RB);
    if (host_fd < 0) {
        klog_info("No saved filesystem found on host ('%s')", FS_IMAGE_FILENAME);
        return -1;
    }

    /* Get file length */
    file_len = semihost_flen(host_fd);
    if (file_len <= 0) {
        klog_warn("Host file is empty or error reading length");
        semihost_close(host_fd);
        return -1;
    }

    if ((size_t)file_len > buffer_size) {
        klog_error("Host file too large (%d bytes, max %u)", (int)file_len, (uint32_t)buffer_size);
        semihost_close(host_fd);
        return -1;
    }

    klog_debug("Reading %d bytes from host file", (int)file_len);

    /* Read file into buffer */
    not_read = semihost_read(host_fd, buffer, (size_t)file_len);
    if (not_read != 0) {
        klog_error("Failed to read host file (%u bytes not read)", (uint32_t)not_read);
        semihost_close(host_fd);
        return -1;
    }

    semihost_close(host_fd);

    /* Validate header */
    header = (fs_image_header_t *)buffer;
    if (header->magic != FS_MAGIC) {
        klog_warn("Invalid filesystem image (bad magic: 0x%x)", header->magic);
        return -1;
    }

    klog_debug("Found filesystem: version %u, %u bytes data", header->version, header->data_size);

    /* Load from buffer into filesystem */
    ret = fs_load(fs, buffer, buffer_size);
    if (ret < 0) {
        klog_error("Failed to deserialize filesystem");
        return -1;
    }

    klog_info("Filesystem loaded from '%s' successfully", FS_IMAGE_FILENAME);
    return 0;
}

/* ============================================================================
 * End of fs_persist.c
 * ============================================================================ */
