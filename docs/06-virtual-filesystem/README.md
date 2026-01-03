# Section 06: Virtual Filesystem

## Overview

This section implements a Virtual File System (VFS) layer with a RAM-based filesystem (ramfs). The VFS provides abstraction for filesystem operations, allowing multiple filesystem types to coexist.

## Components

### VFS Core (vfs.c)
- **Location**: `src/fs/vfs.c`
- **Purpose**: Filesystem abstraction layer
- **Features**:
  - Filesystem registration and mounting
  - Path resolution
  - File descriptor management
  - Generic file operations (open, close, read, write)
  - Directory operations (mkdir, rmdir, readdir)

### RAM Filesystem (ramfs.c)
- **Location**: `src/fs/ramfs.c`
- **Purpose**: In-memory filesystem implementation
- **Features**:
  - Dynamic file/directory creation
  - In-memory data storage
  - Hierarchical directory structure
  - Filename-based lookup

## Architecture

### Three-Layer Design

```
Application Layer
    ↓
VFS Layer (generic operations)
    ↓
Filesystem Implementation (ramfs)
    ↓
Storage (RAM)
```

**Benefits**:
- Applications use generic VFS functions
- Can swap filesystem implementations
- Multiple filesystems can be mounted

## Data Structures

### VFS Inode

```c
typedef struct vfs_inode {
    uint64_t ino;               /* Inode number */
    vfs_file_type_t type;       /* FILE or DIRECTORY */
    uint32_t mode;              /* Permissions (not enforced) */
    uint64_t size;              /* File size in bytes */
    void *fs_data;              /* Filesystem-specific data */
    vfs_filesystem_t *fs;       /* Owning filesystem */
    /* ... other metadata ... */
} vfs_inode_t;
```

**Inode**: Represents a file or directory in the filesystem.

### VFS File

```c
typedef struct vfs_file {
    vfs_inode_t *inode;         /* Associated inode */
    uint32_t flags;             /* O_RDONLY, O_WRONLY, O_RDWR, O_CREAT */
    uint64_t offset;            /* Current position in file */
    uint32_t refcount;          /* Number of references */
    void *private_data;         /* Filesystem-specific */
} vfs_file_t;
```

**File**: Represents an open file. Multiple files can reference same inode.

### File Descriptor Table

```c
typedef struct vfs_fd_table {
    struct {
        vfs_file_t *file;
        uint32_t flags;
    } fds[MAX_OPEN_FILES];      /* 16 FDs per process */
    int next_fd;
} vfs_fd_table_t;
```

**Per-Process**: Each process has its own FD table. FD 0-15 are local to process.

### Ramfs Inode Data

```c
typedef struct ramfs_inode {
    void *data;                 /* File data buffer */
    size_t data_size;           /* Allocated size */
    ramfs_dirent_t *entries;    /* Directory entries (if directory) */
    size_t num_entries;         /* Number of entries */
} ramfs_inode_t;
```

**Type-Specific**: For files, `data` points to content. For directories, `entries` is linked list.

### Ramfs Directory Entry

```c
typedef struct ramfs_dirent {
    char name[MAX_FILENAME_LEN];
    vfs_inode_t *inode;
    struct ramfs_dirent *next;
} ramfs_dirent_t;
```

**Linked List**: Directories maintain list of children.

## API Reference

### VFS Operations

```c
/* Initialize VFS */
void vfs_init(void);

/* Mount filesystem at path */
int vfs_mount(const char *path, vfs_filesystem_t *fs);

/* Open file */
int vfs_open(const char *path, uint32_t flags, uint32_t mode);

/* Close file */
int vfs_close(int fd);

/* Read from file */
ssize_t vfs_read(int fd, void *buf, size_t count);

/* Write to file */
ssize_t vfs_write(int fd, const void *buf, size_t count);

/* Seek in file */
int64_t vfs_seek(int fd, int64_t offset, int whence);

/* Read directory entry */
int vfs_readdir(int fd, vfs_dirent_t *dirent);

/* Create file */
int vfs_create(const char *path, uint32_t mode);

/* Create directory */
int vfs_mkdir(const char *path, uint32_t mode);

/* Delete file */
int vfs_unlink(const char *path);

/* Delete directory */
int vfs_rmdir(const char *path);
```

### Ramfs Operations

```c
/* Create ramfs instance */
vfs_filesystem_t *ramfs_create(void);

/* Destroy ramfs instance */
void ramfs_destroy(vfs_filesystem_t *fs);
```

## Usage Examples

### Initialize and Mount

```c
/* Initialize VFS */
vfs_init();

/* Create ramfs */
vfs_filesystem_t *ramfs = ramfs_create();

/* Mount at root */
vfs_mount("/", ramfs);
```

### File I/O

```c
/* Create and write file */
int fd = vfs_open("/test.txt", O_CREAT | O_WRONLY, 0644);
const char *data = "Hello, World!\n";
vfs_write(fd, data, strlen(data));
vfs_close(fd);

/* Read file */
char buf[100];
fd = vfs_open("/test.txt", O_RDONLY, 0);
ssize_t n = vfs_read(fd, buf, sizeof(buf));
buf[n] = '\0';
kprintf("Read: %s\n", buf);
vfs_close(fd);
```

### Directory Operations

```c
/* Create directory */
vfs_mkdir("/mydir", 0755);

/* Create file in directory */
int fd = vfs_open("/mydir/file.txt", O_CREAT | O_WRONLY, 0644);
vfs_close(fd);

/* List directory */
fd = vfs_open("/mydir", O_RDONLY, 0);
vfs_dirent_t entry;
while (vfs_readdir(fd, &entry) == 0) {
    kprintf("  %s\n", entry.name);
}
vfs_close(fd);
```

## Path Resolution

### Algorithm

```c
int vfs_path_lookup(const char *path, vfs_inode_t **result)
{
    /* Start at root */
    current = vfs.root;

    /* Split path by '/' */
    components = split_path(path);

    /* Walk each component */
    for each component {
        /* Lookup in current directory */
        current->fs->ops->inode_lookup(current, component, &next);
        current = next;
    }

    *result = current;
}
```

**Example**: `/foo/bar/baz`
1. Start at root inode
2. Look up "foo" in root => get foo inode
3. Look up "bar" in foo => get bar inode
4. Look up "baz" in bar => get baz inode
5. Return baz inode

### Absolute vs Relative

- **Absolute**: Starts with `/` (e.g., `/usr/bin`)
- **Relative**: No leading `/` (e.g., `bin/ls`)

Shell resolves relative paths by prepending current working directory.

## File Descriptor Management

### Allocation

```c
int vfs_fd_alloc(vfs_file_t *file, uint32_t flags)
{
    /* Find free FD in current process's table */
    for (fd = 0; fd < MAX_OPEN_FILES; fd++) {
        if (table->fds[fd].file == NULL) {
            table->fds[fd].file = file;
            table->fds[fd].flags = flags;
            file->refcount++;
            return fd;
        }
    }
    return -1;  /* No free FDs */
}
```

**Linear Search**: Simple but O(n). Could use bitmap for O(1).

### Reference Counting

```c
void vfs_fd_free(int fd)
{
    file = table->fds[fd].file;
    file->refcount--;

    /* If no more references, close the file */
    if (file->refcount == 0) {
        file->inode->fs->ops->file_close(file);
        kfree(file);
    }

    table->fds[fd].file = NULL;
}
```

**Multiple References**: Same file can be open multiple times (different FDs, same inode).

## Ramfs Implementation

### File Data Storage

```c
/* Writing to file */
static ssize_t ramfs_file_write(vfs_file_t *file, const void *buf, size_t count)
{
    ramfs_inode_t *ramfs_data = file->inode->fs_data;

    new_size = file->offset + count;

    /* Resize buffer if needed */
    if (new_size > ramfs_data->data_size) {
        new_data = kmalloc(new_size);
        memcpy(new_data, ramfs_data->data, file->inode->size);
        kfree(ramfs_data->data);
        ramfs_data->data = new_data;
        ramfs_data->data_size = new_size;
    }

    /* Write data */
    memcpy(ramfs_data->data + file->offset, buf, count);

    /* Update size and offset */
    file->inode->size = max(file->inode->size, new_size);
    file->offset += count;

    return count;
}
```

**Dynamic Growth**: Buffer reallocates when needed. Fragmentation can occur.

### Directory Listing

```c
static int ramfs_dir_readdir(vfs_file_t *file, vfs_dirent_t **dirent)
{
    ramfs_inode_t *ramfs_data = file->inode->fs_data;

    /* Use offset as entry index */
    index = file->offset;

    if (index >= ramfs_data->num_entries) {
        return -1;  /* No more entries */
    }

    /* Find entry at index */
    entry = ramfs_data->entries;
    for (i = 0; i < index; i++) {
        entry = entry->next;
    }

    /* Convert to VFS dirent */
    static vfs_dirent_t vfs_entry;
    vfs_entry.ino = entry->inode->ino;
    vfs_entry.type = entry->inode->type;
    strcpy(vfs_entry.name, entry->name);

    *dirent = &vfs_entry;
    file->offset++;

    return 0;
}
```

**Static Buffer**: Returns pointer to static buffer. Caller must copy before next call.

**Offset as Index**: File offset tracks position in directory listing.

## Known Issues

### Struct Assignment Bug (Critical)

AArch64 GCC can generate incorrect code for struct assignment with optimization enabled:

```c
/* Wrong - can corrupt stack */
*dirent = vfs_entry;

/* Correct - manual field copy */
dirent->ino = vfs_entry.ino;
dirent->type = vfs_entry.type;
strcpy(dirent->name, vfs_entry.name);
```

The VFS code manually copies fields to avoid this compiler bug.

### No Persistence

Ramfs is entirely in RAM. All data lost when QEMU exits.

**Attempted Solutions**: VirtIO block and pflash drivers were tried but both failed with QEMU.

### Limited File Size

Files limited to RAMFS_MAX_FILE_SIZE (1MB). Arbitrary limit to prevent memory exhaustion.

### No Permissions

Mode bits are stored but not enforced. All processes can read/write all files.

### Inode Numbers Not Unique Across Mounts

Each ramfs instance starts inode numbers at 1. If multiple ramfs instances are mounted, inode collisions can occur.

### Memory Leaks

Deleted files' memory is freed, but inode numbers are never reused.

## Testing

### Basic File Operations

```c
int fd = vfs_open("/test", O_CREAT | O_WRONLY, 0644);
assert(fd >= 0);

vfs_write(fd, "data", 4);
vfs_close(fd);

fd = vfs_open("/test", O_RDONLY, 0);
char buf[10];
ssize_t n = vfs_read(fd, buf, 10);
assert(n == 4);
assert(memcmp(buf, "data", 4) == 0);
```

### Directory Operations

```c
vfs_mkdir("/dir", 0755);
vfs_open("/dir/file", O_CREAT, 0644);

int fd = vfs_open("/dir", O_RDONLY, 0);
vfs_dirent_t entry;
while (vfs_readdir(fd, &entry) == 0) {
    kprintf("%s\n", entry.name);
}
/* Should print: file */
```

### Path Resolution

```c
vfs_mkdir("/a", 0755);
vfs_mkdir("/a/b", 0755);
vfs_mkdir("/a/b/c", 0755);

int fd = vfs_open("/a/b/c/file.txt", O_CREAT, 0644);
assert(fd >= 0);
```