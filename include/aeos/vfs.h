/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/vfs.h
 * Description: Virtual File System interface
 * ============================================================================ */

#ifndef AEOS_VFS_H
#define AEOS_VFS_H

#include <aeos/types.h>

/* Maximum number of open files per process */
#define MAX_OPEN_FILES     16

/* Maximum path length */
#define MAX_PATH_LEN       256

/* Maximum filename length */
#define MAX_FILENAME_LEN   64

/* File types */
typedef enum {
    VFS_FILE_REGULAR = 0,   /* Regular file */
    VFS_FILE_DIRECTORY,     /* Directory */
    VFS_FILE_DEVICE,        /* Device file */
    VFS_FILE_SYMLINK        /* Symbolic link */
} vfs_file_type_t;

/* File access modes (for open) */
#define O_RDONLY    0x0001  /* Read only */
#define O_WRONLY    0x0002  /* Write only */
#define O_RDWR      0x0003  /* Read and write */
#define O_CREAT     0x0100  /* Create if doesn't exist */
#define O_TRUNC     0x0200  /* Truncate to zero length */
#define O_APPEND    0x0400  /* Append mode */
#define O_EXCL      0x0800  /* Exclusive create (fail if exists) */

/* Seek whence values */
#define SEEK_SET    0       /* Seek from beginning */
#define SEEK_CUR    1       /* Seek from current position */
#define SEEK_END    2       /* Seek from end */

/* VFS inode - represents a file in the filesystem */
typedef struct vfs_inode {
    uint64_t ino;               /* Inode number */
    vfs_file_type_t type;       /* File type */
    uint32_t mode;              /* Access permissions */
    uint32_t uid;               /* Owner user ID */
    uint32_t gid;               /* Owner group ID */
    uint64_t size;              /* File size in bytes */
    uint64_t blocks;            /* Number of blocks allocated */
    uint64_t atime;             /* Last access time */
    uint64_t mtime;             /* Last modification time */
    uint64_t ctime;             /* Creation time */
    uint32_t nlinks;            /* Number of hard links */
    void *fs_data;              /* Filesystem-specific data */
    struct vfs_filesystem *fs;  /* Filesystem this inode belongs to */
} vfs_inode_t;

/* Directory entry */
typedef struct vfs_dirent {
    uint64_t ino;                       /* Inode number */
    vfs_file_type_t type;               /* File type */
    uint64_t size;                      /* File size in bytes */
    char name[MAX_FILENAME_LEN];        /* Filename */
    struct vfs_dirent *next;            /* Next entry (for readdir) */
} vfs_dirent_t;

/* Forward declaration */
struct vfs_file;

/* Filesystem operations - function pointers implemented by specific filesystems */
typedef struct vfs_fs_ops {
    /* Inode operations */
    int (*inode_lookup)(vfs_inode_t *parent, const char *name, vfs_inode_t **result);
    int (*inode_create)(vfs_inode_t *parent, const char *name, uint32_t mode, vfs_inode_t **result);
    int (*inode_mkdir)(vfs_inode_t *parent, const char *name, uint32_t mode, vfs_inode_t **result);
    int (*inode_unlink)(vfs_inode_t *parent, const char *name);
    int (*inode_rmdir)(vfs_inode_t *parent, const char *name);

    /* File operations */
    ssize_t (*file_read)(struct vfs_file *file, void *buf, size_t count);
    ssize_t (*file_write)(struct vfs_file *file, const void *buf, size_t count);
    int (*file_close)(struct vfs_file *file);

    /* Directory operations */
    int (*dir_readdir)(struct vfs_file *file, vfs_dirent_t **dirent);
} vfs_fs_ops_t;

/* Filesystem type registration */
typedef struct vfs_filesystem {
    char name[32];              /* Filesystem type name (e.g., "ramfs", "devfs") */
    vfs_fs_ops_t *ops;          /* Filesystem operations */
    vfs_inode_t *root;          /* Root inode of this filesystem */
    void *fs_data;              /* Filesystem-specific data */
    struct vfs_filesystem *next; /* Next registered filesystem */
} vfs_filesystem_t;

/* Open file handle */
typedef struct vfs_file {
    vfs_inode_t *inode;         /* Inode of the file */
    uint32_t flags;             /* Open flags (O_RDONLY, etc.) */
    uint64_t offset;            /* Current file position */
    uint32_t refcount;          /* Reference count */
    void *private_data;         /* Filesystem-specific data */
} vfs_file_t;

/* File descriptor - per-process handle to open file */
typedef struct vfs_fd {
    vfs_file_t *file;           /* Pointer to open file */
    uint32_t flags;             /* File descriptor flags */
} vfs_fd_t;

/* File descriptor table - per process */
typedef struct vfs_fd_table {
    vfs_fd_t fds[MAX_OPEN_FILES];
    uint32_t next_fd;           /* Next available fd */
} vfs_fd_table_t;

/* Mount point */
typedef struct vfs_mount {
    char path[MAX_PATH_LEN];    /* Mount point path */
    vfs_filesystem_t *fs;       /* Mounted filesystem */
    vfs_inode_t *mountpoint;    /* Inode of mount point */
    struct vfs_mount *next;     /* Next mount point */
} vfs_mount_t;

/* ============================================================================
 * VFS Core Functions
 * ============================================================================ */

/**
 * Initialize VFS subsystem
 */
void vfs_init(void);

/**
 * Register a filesystem type
 */
int vfs_register_filesystem(vfs_filesystem_t *fs);

/**
 * Mount a filesystem at a path
 */
int vfs_mount(const char *path, vfs_filesystem_t *fs);

/**
 * Unmount a filesystem
 */
int vfs_unmount(const char *path);

/* ============================================================================
 * File Operations (syscall interface)
 * ============================================================================ */

/**
 * Open a file
 * @return File descriptor on success, negative on error
 */
int vfs_open(const char *path, uint32_t flags, uint32_t mode);

/**
 * Close a file descriptor
 */
int vfs_close(int fd);

/**
 * Read from a file
 */
ssize_t vfs_read(int fd, void *buf, size_t count);

/**
 * Write to a file
 */
ssize_t vfs_write(int fd, const void *buf, size_t count);

/**
 * Seek to a position in a file
 */
int64_t vfs_seek(int fd, int64_t offset, int whence);

/* ============================================================================
 * Directory Operations
 * ============================================================================ */

/**
 * Read directory entry
 */
int vfs_readdir(int fd, vfs_dirent_t *dirent);

/* ============================================================================
 * File Descriptor Table Operations
 * ============================================================================ */

/**
 * Create a new file descriptor table for a process
 */
vfs_fd_table_t *vfs_fd_table_create(void);

/**
 * Destroy a file descriptor table
 */
void vfs_fd_table_destroy(vfs_fd_table_t *table);

/**
 * Allocate a file descriptor in the current process
 */
int vfs_fd_alloc(vfs_file_t *file, uint32_t flags);

/**
 * Get file from file descriptor
 */
vfs_file_t *vfs_fd_to_file(int fd);

/**
 * Free a file descriptor
 */
void vfs_fd_free(int fd);

/* ============================================================================
 * Path Resolution
 * ============================================================================ */

/**
 * Resolve a path to an inode
 */
int vfs_path_lookup(const char *path, vfs_inode_t **result);

/**
 * Create a new file at path
 */
int vfs_create(const char *path, uint32_t mode);

/**
 * Create a new directory
 */
int vfs_mkdir(const char *path, uint32_t mode);

/**
 * Remove a file
 */
int vfs_unlink(const char *path);

/**
 * Remove a directory
 */
int vfs_rmdir(const char *path);

/**
 * Get the root filesystem
 */
vfs_filesystem_t *vfs_get_root_fs(void);

#endif /* AEOS_VFS_H */

/* ============================================================================
 * End of vfs.h
 * ============================================================================ */
