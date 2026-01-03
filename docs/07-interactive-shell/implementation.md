# Interactive Shell - Implementation Details

## Shell Initialization and Main Loop

### shell_run()

```c
void shell_run(void)
{
    char line[SHELL_MAX_LINE];      /* 256 bytes */
    char *argv[SHELL_MAX_ARGS];     /* 16 pointers */
    int argc;

    print_banner();

    while (1) {
        kprintf(PROMPT);                /* "AEOS> " */
        shell_readline(line, SHELL_MAX_LINE);
        shell_parse(line, &argc, argv);
        if (argc > 0) {
            shell_execute(argc, argv);
        }
    }
}
```

**Infinite Loop**: Never returns. Shell is the main user interface.

**Stack Usage**: `line` buffer (256 bytes) + `argv` array (128 bytes) = ~400 bytes per iteration.

## Line Input with Backspace

### shell_readline()

```c
int shell_readline(char *buf, int len)
{
    int pos = 0;
    char c;

    while (1) {
        c = uart_getc();  /* Blocking read */

        if (c == '\r' || c == '\n') {
            /* End of line */
            uart_putc('\n');
            buf[pos] = '\0';
            return pos;
        } else if (c == '\b' || c == 0x7F) {
            /* Backspace or Delete */
            if (pos > 0) {
                pos--;
                /* Erase character: back, space, back */
                uart_putc('\b');
                uart_putc(' ');
                uart_putc('\b');
            }
        } else if (c >= 32 && c < 127) {
            /* Printable ASCII */
            if (pos < len - 1) {
                buf[pos++] = c;
                uart_putc(c);  /* Echo character */
            }
        }
        /* Ignore other control characters */
    }
}
```

**Blocking**: Waits for each character. CPU spins in UART polling loop.

**Echo**: Characters are echoed as typed for visual feedback.

**Backspace Handling**: Both `\b` (0x08) and DEL (0x7F) treated as backspace.

## Command Parsing

### shell_parse()

```c
int shell_parse(char *line, int *argc, char **argv)
{
    int i = 0;
    char *p = line;

    *argc = 0;

    /* Skip leading whitespace */
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    /* Parse arguments */
    while (*p != '\0' && i < SHELL_MAX_ARGS) {
        /* Start of argument */
        argv[i++] = p;

        /* Find end of argument */
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }

        /* Null-terminate argument */
        if (*p != '\0') {
            *p++ = '\0';
            /* Skip whitespace */
            while (*p == ' ' || *p == '\t') {
                p++;
            }
        }
    }

    *argc = i;
    return 0;
}
```

**In-Place Modification**: Modifies `line` buffer by inserting null terminators.

**argv Pointers**: Point into `line` buffer. No separate allocations.

**Example**:
```
Input:  "ls  -la  /home"
After:  "ls\0-la\0/home\0"
argv[0] => "ls"
argv[1] => "-la"
argv[2] => "/home"
argc = 3
```

## Command Execution

### shell_execute()

```c
int shell_execute(int argc, char **argv)
{
    if (argc == 0) {
        return 0;
    }

    /* Look for built-in command */
    for (int i = 0; builtin_commands[i].name != NULL; i++) {
        if (strcmp(argv[0], builtin_commands[i].name) == 0) {
            return builtin_commands[i].func(argc, argv);
        }
    }

    /* Command not found */
    kprintf("%s: command not found\n", argv[0]);
    kprintf("Type 'help' for available commands\n");
    return -1;
}
```

**Linear Search**: O(n) lookup through command table. Fine for small number of commands.

**No External Programs**: Only built-in commands supported. No exec().

## Path Resolution

### resolve_path()

```c
static const char *resolve_path(const char *path)
{
    static char resolved[512];

    /* Empty or NULL => current directory */
    if (path == NULL || path[0] == '\0') {
        return cwd;
    }

    /* Absolute path */
    if (path[0] == '/') {
        return path;
    }

    /* Relative path - combine with cwd */
    if (strcmp(cwd, "/") == 0) {
        /* Root directory - don't add extra slash */
        resolved[0] = '/';
        resolved[1] = '\0';
        strncat(resolved, path, sizeof(resolved) - 2);
    } else {
        /* Non-root directory */
        strncpy(resolved, cwd, sizeof(resolved) - 1);
        resolved[sizeof(resolved) - 1] = '\0';

        size_t len = strlen(resolved);
        if (len < sizeof(resolved) - 2) {
            resolved[len] = '/';
            resolved[len + 1] = '\0';
            strncat(resolved, path, sizeof(resolved) - len - 2);
        }
    }

    return resolved;
}
```

**Static Buffer**: Returns pointer to static storage. Not thread-safe.

**Buffer Overflow Protection**: Uses `strncpy` and `strncat` with size limits.

## Individual Command Implementations

### cmd_ls()

```c
static int cmd_ls(int argc, char **argv)
{
    const char *path = (argc > 1) ? resolve_path(argv[1]) : cwd;

    /* Open directory */
    int fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0) {
        kprintf("ls: cannot access '%s'\n", path);
        return -1;
    }

    kprintf("\nListing: %s\n", path);
    kprintf("------------------------------------------------------\n");

    /* Read directory entries */
    vfs_dirent_t entry;
    while (vfs_readdir(fd, &entry) == 0) {
        if (entry.type == VFS_FILE_DIRECTORY) {
            kprintf("  [DIR]  %s/\n", entry.name);
        } else {
            kprintf("  [FILE] %s (%u bytes)\n",
                    entry.name, (uint32_t)entry.size);
        }
    }

    kprintf("------------------------------------------------------\n\n");

    vfs_close(fd);
    return 0;
}
```

**Entry Format**: Type indicator + name (+ size for files).

**Directory Slash**: Appends `/` to directory names for clarity.

### cmd_cd()

```c
static int cmd_cd(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/";

    /* Handle ".." (parent directory) */
    if (strcmp(path, "..") == 0) {
        size_t len = strlen(cwd);
        if (len > 1) {
            /* Find last '/' */
            char *last_slash = NULL;
            for (size_t i = len - 1; i > 0; i--) {
                if (cwd[i] == '/') {
                    last_slash = &cwd[i];
                    break;
                }
            }

            if (last_slash == cwd) {
                /* Parent is root */
                strcpy(cwd, "/");
            } else if (last_slash != NULL) {
                /* Truncate at last slash */
                *last_slash = '\0';
            } else {
                /* No slash found */
                strcpy(cwd, "/");
            }
        }
        return 0;
    }

    /* Handle "." (current directory) */
    if (strcmp(path, ".") == 0) {
        return 0;
    }

    /* Build absolute path */
    char new_cwd[256];
    if (path[0] == '/') {
        strncpy(new_cwd, path, sizeof(new_cwd) - 1);
    } else {
        /* Relative path */
        const char *resolved = resolve_path(path);
        strncpy(new_cwd, resolved, sizeof(new_cwd) - 1);
    }
    new_cwd[sizeof(new_cwd) - 1] = '\0';

    /* Verify directory exists */
    int fd = vfs_open(new_cwd, O_RDONLY, 0);
    if (fd < 0) {
        kprintf("cd: %s: No such file or directory\n", path);
        return -1;
    }
    vfs_close(fd);

    /* Update cwd */
    strncpy(cwd, new_cwd, sizeof(cwd) - 1);
    cwd[sizeof(cwd) - 1] = '\0';

    /* Remove trailing slash (except for root) */
    size_t len = strlen(cwd);
    if (len > 1 && cwd[len - 1] == '/') {
        cwd[len - 1] = '\0';
    }

    return 0;
}
```

**Parent Directory Algorithm**:
1. Find last `/` in current path
2. Truncate path at that position
3. Special case: if last `/` is at index 0, path becomes "/"

### cmd_cat()

```c
static int cmd_cat(int argc, char **argv)
{
    if (argc < 2) {
        kprintf("Usage: cat <filename>\n");
        return -1;
    }

    const char *path = resolve_path(argv[1]);

    /* Open file */
    int fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0) {
        kprintf("cat: %s: No such file or directory\n", argv[1]);
        return -1;
    }

    /* Read and print file contents */
    char buffer[256];
    ssize_t bytes_read;

    kprintf("\n");
    while (1) {
        bytes_read = vfs_read(fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            break;
        }

        buffer[bytes_read] = '\0';  /* Null-terminate */
        kprintf("%s", buffer);
    }
    kprintf("\n");

    vfs_close(fd);
    return 0;
}
```

**Chunk Reading**: Reads file in 256-byte chunks for efficiency.

**Null Termination**: Adds null terminator for kprintf().

### cmd_cp()

```c
static int cmd_cp(int argc, char **argv)
{
    if (argc < 3) {
        kprintf("Usage: cp <source> <destination>\n");
        return -1;
    }

    const char *src_path = resolve_path(argv[1]);
    const char *dst_path = resolve_path(argv[2]);

    /* Open source for reading */
    int src_fd = vfs_open(src_path, O_RDONLY, 0);
    if (src_fd < 0) {
        kprintf("cp: cannot open '%s'\n", argv[1]);
        return -1;
    }

    /* Create destination for writing */
    int dst_fd = vfs_open(dst_path, O_CREAT | O_WRONLY, 0644);
    if (dst_fd < 0) {
        kprintf("cp: cannot create '%s'\n", argv[2]);
        vfs_close(src_fd);
        return -1;
    }

    /* Copy data */
    char buffer[512];
    ssize_t bytes_read, bytes_written;

    while (1) {
        bytes_read = vfs_read(src_fd, buffer, sizeof(buffer));
        if (bytes_read <= 0) {
            break;
        }

        bytes_written = vfs_write(dst_fd, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            kprintf("cp: write error\n");
            vfs_close(src_fd);
            vfs_close(dst_fd);
            return -1;
        }
    }

    kprintf("Copied: %s -> %s\n", argv[1], argv[2]);

    vfs_close(src_fd);
    vfs_close(dst_fd);
    return 0;
}
```

**Buffer Size**: 512 bytes balances stack usage and copy efficiency.

**Error Checking**: Verifies write succeeded before continuing.

### cmd_rm()

```c
static int cmd_rm(int argc, char **argv)
{
    int arg_idx = 1;
    int recursive = 0;
    int force = 0;

    /* Parse flags */
    while (arg_idx < argc && argv[arg_idx][0] == '-') {
        char *flag = argv[arg_idx] + 1;  /* Skip '-' */

        while (*flag != '\0') {
            if (*flag == 'r') {
                recursive = 1;
            } else if (*flag == 'f') {
                force = 1;
            } else {
                kprintf("rm: invalid option -- '%c'\n", *flag);
                return -1;
            }
            flag++;
        }

        arg_idx++;
    }

    if (arg_idx >= argc) {
        kprintf("Usage: rm [-rf] <file|directory>\n");
        return -1;
    }

    const char *target = resolve_path(argv[arg_idx]);

    /* Try unlink (file) first */
    int ret = vfs_unlink(target);
    if (ret < 0) {
        /* If unlink failed, try rmdir if recursive */
        if (recursive) {
            ret = vfs_rmdir(target);
            if (ret < 0 && !force) {
                kprintf("rm: cannot remove '%s'\n", target);
                return -1;
            }
        } else if (!force) {
            kprintf("rm: cannot remove '%s': Is a directory (use -r)\n", target);
            return -1;
        }
    }

    if (ret == 0) {
        kprintf("Removed: %s\n", target);
    }

    return 0;
}
```

**Flag Parsing**: Supports combined flags like `-rf`.

**Try File First**: Attempts `unlink()`, falls back to `rmdir()`.

## Debugging

### Trace Command Execution

```c
/* Add to shell_execute() */
kprintf("[DEBUG] Executing: %s (argc=%d)\n", argv[0], argc);
```

### Dump argv

```c
/* Add to shell_execute() */
for (int i = 0; i < argc; i++) {
    kprintf("  argv[%d] = '%s'\n", i, argv[i]);
}
```

### Check CWD

```c
/* Add to cmd_cd() */
kprintf("[DEBUG] cwd before: %s\n", cwd);
/* ... change directory ... */
kprintf("[DEBUG] cwd after: %s\n", cwd);
```

## Performance Considerations

### Blocking I/O
`shell_readline()` blocks on `uart_getc()`. No async I/O or multitasking during input.

### String Operations
Path resolution and parsing use string operations. O(n) where n is path length.

### Command Lookup
Linear search through command table. O(n) where n is number of commands. Could use hash table but it won't matter much for small n.

## Common Mistakes

### Modifying argv After parse
```c
/* Wrong */
shell_parse(line, &argc, argv);
strcat(argv[0], "_modified");  /* Modifies line buffer */
```

### Assuming CWD is Per-Process
```c
/* CWD is global, not per-process */
/* If one process changes it, all processes see the change */
```

### Buffer Overruns in Path Building
```c
/* Dangerous */
sprintf(path, "%s/%s", cwd, filename);  /* No size check */

/* Safe */
snprintf(path, sizeof(path), "%s/%s", cwd, filename);
```