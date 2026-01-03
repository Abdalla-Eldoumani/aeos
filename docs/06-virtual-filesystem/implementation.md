# Virtual Filesystem - Implementation Details

## Key Implementation Details

Due to the comprehensive nature of the VFS implementation, this document highlights the most critical implementation aspects and common pitfalls.

## Struct Assignment Bug (Critical)

The AArch64 compiler with optimizations can generate incorrect code for struct assignments on stack variables:

```c
/* Dangerous - can corrupt stack on ARM64 */
vfs_dirent_t entry;
entry = *some_struct;  /* memcpy optimization may overwrite adjacent stack */

/* Safe - manual field copying */
entry.ino = some_struct->ino;
entry.type = some_struct->type;
strncpy(entry.name, some_struct->name, MAX_FILENAME_LEN);
```

**Location in Code**: `vfs.c:vfs_readdir()`

**Why It Happens**: GCC optimizes struct assignment into `memcpy()` or block move instructions that may not respect stack boundaries.

**Solution**: Manual field-by-field copying for large structs.

## Path Splitting Implementation

```c
static int split_path(const char *path, char **components, int max_components)
{
    char *path_copy = kmalloc(strlen(path) + 1);
    strcpy(path_copy, path);

    /* Tokenize by '/' */
    char *saveptr;
    char *token = strtok_r(path_copy, "/", &saveptr);
    int count = 0;

    while (token != NULL && count < max_components) {
        components[count] = kmalloc(strlen(token) + 1);
        strcpy(components[count], token);
        count++;
        token = strtok_r(NULL, "/", &saveptr);
    }

    kfree(path_copy);
    return count;
}
```

**Memory Management**: Each component is separately allocated. Caller must free all.

**Error Handling**: If allocation fails mid-way, previously allocated components are freed.

## Ramfs Inode Creation

```c
static vfs_inode_t *ramfs_create_inode(vfs_file_type_t type, uint32_t mode)
{
    /* Allocate VFS inode */
    vfs_inode_t *inode = kmalloc(sizeof(vfs_inode_t));

    /* Allocate ramfs-specific data */
    ramfs_inode_t *ramfs_data = kmalloc(sizeof(ramfs_inode_t));

    /* Initialize inode */
    inode->ino = next_ino++;  /* Global counter */
    inode->type = type;
    inode->mode = mode;
    inode->size = 0;
    inode->fs_data = ramfs_data;

    /* Initialize ramfs data */
    ramfs_data->data = NULL;
    ramfs_data->data_size = 0;
    ramfs_data->entries = NULL;
    ramfs_data->num_entries = 0;

    return inode;
}
```

**Two-Level Structure**: VFS inode + filesystem-specific data.

**Inode Numbers**: Global counter - not reset when filesystem is unmounted.

## File Creation with Parent Lookup

```c
int vfs_create(const char *path, uint32_t mode)
{
    /* Split path to get parent directory and filename */
    components = split_path(path);
    filename = components[num_components - 1];

    /* Build parent directory path */
    if (num_components == 1) {
        /* Creating in root */
        parent_inode = vfs.root;
    } else {
        /* Build parent path and lookup */
        char *parent_path = build_parent_path(components, num_components - 1);
        vfs_path_lookup(parent_path, &parent_inode);
        kfree(parent_path);
    }

    /* Call filesystem's create function */
    parent_inode->fs->ops->inode_create(parent_inode, filename, mode, &new_inode);

    /* Cleanup */
    for (i = 0; i < num_components; i++) {
        kfree(components[i]);
    }

    return 0;
}
```

**Path Decomposition**: Split "/foo/bar/file.txt" into parent "/foo/bar" and name "file.txt".

**Filesystem Delegation**: VFS layer resolves path, filesystem layer creates inode.

## Ramfs File Write with Dynamic Growth

```c
static ssize_t ramfs_file_write(vfs_file_t *file, const void *buf, size_t count)
{
    ramfs_inode_t *ramfs_data = file->inode->fs_data;

    /* Calculate new file size */
    size_t new_size = file->offset + count;

    /* Check size limit */
    if (new_size > RAMFS_MAX_FILE_SIZE) {
        return -1;
    }

    /* Resize buffer if needed */
    if (new_size > ramfs_data->data_size) {
        char *new_data = kmalloc(new_size);
        if (!new_data) return -1;

        /* Copy existing data */
        if (ramfs_data->data) {
            memcpy(new_data, ramfs_data->data, file->inode->size);
            kfree(ramfs_data->data);
        }

        ramfs_data->data = new_data;
        ramfs_data->data_size = new_size;
    }

    /* Write data */
    memcpy(ramfs_data->data + file->offset, buf, count);

    /* Update file size if extended */
    if (new_size > file->inode->size) {
        file->inode->size = new_size;
    }

    file->offset += count;

    return count;
}
```

**Dynamic Allocation**: Buffer grows as needed. No pre-allocation.

**Sparse Files Not Supported**: Seeking past end and writing creates data at that offset, but doesn't zero-fill gaps.

**Memory Fragmentation**: Each resize allocates new buffer and frees old one.

## Directory Entry Management

```c
static int ramfs_inode_create(vfs_inode_t *parent, const char *name,
                               uint32_t mode, vfs_inode_t **result)
{
    ramfs_inode_t *parent_data = parent->fs_data;

    /* Check if already exists */
    ramfs_dirent_t *entry = parent_data->entries;
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            return -1;  /* Already exists */
        }
        entry = entry->next;
    }

    /* Create new inode */
    vfs_inode_t *new_inode = ramfs_create_inode(VFS_FILE_REGULAR, mode);
    new_inode->fs = parent->fs;

    /* Create directory entry */
    ramfs_dirent_t *new_entry = kmalloc(sizeof(ramfs_dirent_t));
    strncpy(new_entry->name, name, sizeof(new_entry->name) - 1);
    new_entry->inode = new_inode;

    /* Add to head of list */
    new_entry->next = parent_data->entries;
    parent_data->entries = new_entry;
    parent_data->num_entries++;

    *result = new_inode;
    return 0;
}
```

**Linked List**: Directory entries in singly-linked list.

**No Ordering**: Entries added to head (reverse chronological order).

**Duplicate Check**: Linear search through existing entries.

## File Descriptor Table Operations

```c
int vfs_fd_alloc(vfs_file_t *file, uint32_t flags)
{
    process_t *proc = process_current();
    vfs_fd_table_t *table = proc->fd_table;

    /* Find free FD */
    for (int fd = 0; fd < MAX_OPEN_FILES; fd++) {
        if (table->fds[fd].file == NULL) {
            table->fds[fd].file = file;
            table->fds[fd].flags = flags;
            file->refcount++;
            return fd;
        }
    }

    return -1;  /* No free FDs */
}

void vfs_fd_free(int fd)
{
    process_t *proc = process_current();
    vfs_fd_table_t *table = proc->fd_table;
    vfs_file_t *file = table->fds[fd].file;

    if (file == NULL) return;

    file->refcount--;

    /* If no more references, close file */
    if (file->refcount == 0) {
        if (file->inode && file->inode->fs && file->inode->fs->ops->file_close) {
            file->inode->fs->ops->file_close(file);
        }
        kfree(file);
    }

    table->fds[fd].file = NULL;
}
```

**Reference Counting**: Multiple FDs can point to same file. Closed when refcount reaches 0.

**Table Per Process**: Each process has independent FD space.

## Filesystem Operations Dispatch

```c
ssize_t vfs_write(int fd, const void *buf, size_t count)
{
    vfs_file_t *file = vfs_fd_to_file(fd);
    if (!file) return -1;

    /* Check filesystem supports write */
    if (!file->inode->fs || !file->inode->fs->ops ||
        !file->inode->fs->ops->file_write) {
        return -1;
    }

    /* Call filesystem-specific write */
    return file->inode->fs->ops->file_write(file, buf, count);
}
```

**Indirection**: VFS => inode => fs => ops => file_write

**Null Checks**: Validate entire chain before calling.

## Debugging Tips

### Trace File Operations

```c
/* Add to vfs_open() */
klog_debug("vfs_open: %s flags=0x%x", path, flags);

/* Add to vfs_write() */
klog_debug("vfs_write: fd=%d count=%u", fd, count);
```

### Dump Directory Contents

```c
void dump_directory(vfs_inode_t *dir)
{
    ramfs_inode_t *data = dir->fs_data;
    ramfs_dirent_t *entry = data->entries;

    kprintf("Directory (ino=%llu):\n", dir->ino);
    while (entry) {
        kprintf("  %s (ino=%llu, type=%d)\n",
                entry->name, entry->inode->ino, entry->inode->type);
        entry = entry->next;
    }
}
```

### Check for Memory Leaks

```c
heap_stats_t before, after;

heap_get_stats(&before);

/* Perform file operations */
int fd = vfs_open("/test", O_CREAT | O_WRONLY, 0644);
vfs_write(fd, "data", 4);
vfs_close(fd);
vfs_unlink("/test");

heap_get_stats(&after);

kprintf("Heap delta: %d bytes\n", after.used_size - before.used_size);
/* Should be 0 if no leaks */
```

### Verify Inode Cleanup

```c
/* Add to ramfs_inode_unlink() */
klog_debug("Freeing inode %llu", entry->inode->ino);
```

## Performance Considerations

### Path Lookup: O(n)
Each component requires linear search through directory entries.

**Improvement**: Hash table for directory lookups.

### File Write: O(size)
Writing requires allocating new buffer and copying existing data.

**Improvement**: Slab allocator or extent-based storage.

### Directory Listing: O(n)
readdir() walks linked list from start each time.

**Improvement**: Maintain position pointer in file structure.

## Common Mistakes

### Forgetting to Free Path Components

```c
/* Wrong */
char **components = split_path(path);
/* ... use components ... */
/* components leaked! */

/* Correct */
char **components = split_path(path);
/* ... use components ... */
for (int i = 0; i < num; i++) {
    kfree(components[i]);
}
```

### Not Checking File Type Before Operations

```c
/* Wrong */
vfs_write(fd, buf, len);  /* Might be a directory */

/* Correct */
vfs_file_t *file = vfs_fd_to_file(fd);
if (file->inode->type != VFS_FILE_REGULAR) {
    return -1;
}
vfs_write(fd, buf, len);
```

### Assuming Null-Terminated Paths

```c
/* Wrong */
strcpy(resolved, path);  /* Path might not be null-terminated */

/* Correct */
strncpy(resolved, path, sizeof(resolved) - 1);
resolved[sizeof(resolved) - 1] = '\0';
```