# Section 06: Virtual Filesystem

## Overview

This section implements a Virtual File System (VFS) layer with a RAM-based filesystem (ramfs) and host persistence via ARM semihosting. The VFS provides abstraction for filesystem operations, allowing multiple filesystem types to coexist.

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

### Persistence (fs_persist.c + semihosting.c)
- **Location**: `src/fs/fs_persist.c`, `src/drivers/semihosting.c`
- **Purpose**: Save/load filesystem to host
- **Features**:
  - Serialize ramfs to binary format
  - Write to host via ARM semihosting
  - Auto-load on boot if file exists
  - Saves to `aeos_fs.img` on host

## Architecture

### Three-Layer Design

```
Application Layer (shell commands)
    ↓
VFS Layer (generic operations)
    ↓
Filesystem Implementation (ramfs)
    ↓
Storage (RAM + semihosting persistence)
```

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
} vfs_inode_t;
```

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

/* Create directory */
int vfs_mkdir(const char *path, uint32_t mode);

/* Delete file */
int vfs_unlink(const char *path);

/* Delete directory */
int vfs_rmdir(const char *path);
```

### Persistence Operations

```c
/* Save filesystem to host */
int fs_save_to_disk(void);

/* Load filesystem from host */
int fs_load_from_disk(void);
```

## Persistence via Semihosting

ARM semihosting allows the guest OS to make file operations on the host filesystem. AEOS uses this to persist the ramfs contents.

### How It Works

1. `save` command triggers `fs_save_to_disk()`
2. Ramfs is serialized to a binary format
3. Semihosting `SYS_OPEN`, `SYS_WRITE`, `SYS_CLOSE` write to host
4. File is saved as `aeos_fs.img` in the QEMU working directory
5. On boot, `fs_load_from_disk()` checks for existing file
6. If found, ramfs is restored from the saved state

### Semihosting Implementation

```c
/* ARM semihosting uses HLT instruction */
static inline long semihost_call(int op, void *arg)
{
    register long r0 __asm__("x0") = op;
    register void *r1 __asm__("x1") = arg;
    __asm__ volatile("hlt #0xF000" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
```

### QEMU Configuration

Semihosting requires QEMU flag (already in Makefile):
```
-semihosting-config enable=on,target=native
```

## Usage Examples

### File I/O

```c
/* Create and write file */
int fd = vfs_open("/test.txt", O_CREAT | O_WRONLY, 0644);
vfs_write(fd, "Hello, World!\n", 14);
vfs_close(fd);

/* Read file */
char buf[100];
fd = vfs_open("/test.txt", O_RDONLY, 0);
ssize_t n = vfs_read(fd, buf, sizeof(buf));
vfs_close(fd);
```

### Directory Operations

```c
/* Create directory */
vfs_mkdir("/mydir", 0755);

/* List directory */
int fd = vfs_open("/mydir", O_RDONLY, 0);
vfs_dirent_t entry;
while (vfs_readdir(fd, &entry) == 0) {
    kprintf("  %s\n", entry.name);
}
vfs_close(fd);
```

### Persistence

```
AEOS> touch myfile.txt
AEOS> write myfile.txt Important data
AEOS> save
Filesystem saved successfully!
[Exit QEMU, restart]
AEOS> cat myfile.txt
Important data
```

## Path Resolution

### Algorithm

1. Start at root inode
2. Split path by `/`
3. For each component, look up in current directory
4. Return final inode

**Example**: `/foo/bar/baz`
- Look up "foo" in root → foo inode
- Look up "bar" in foo → bar inode
- Look up "baz" in bar → baz inode

## Known Issues

### Struct Assignment Bug

AArch64 GCC can generate incorrect code for struct assignment:

```c
/* Dangerous - can corrupt stack */
*dirent = vfs_entry;

/* Safe - manual field copy */
dirent->ino = vfs_entry.ino;
dirent->type = vfs_entry.type;
strcpy(dirent->name, vfs_entry.name);
```

The VFS code manually copies fields to avoid this.

### Limited File Size

Files limited to 1MB (RAMFS_MAX_FILE_SIZE) to prevent memory exhaustion.

### No Permissions

Mode bits are stored but not enforced. All processes can read/write all files.

### Memory Usage

Entire filesystem lives in RAM. Large files consume heap space.
