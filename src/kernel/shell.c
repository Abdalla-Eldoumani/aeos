/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/kernel/shell.c
 * Description: Interactive shell implementation
 * ============================================================================ */

#include <aeos/shell.h>
#include <aeos/kprintf.h>
#include <aeos/uart.h>
#include <aeos/string.h>
#include <aeos/scheduler.h>
#include <aeos/pmm.h>
#include <aeos/heap.h>
#include <aeos/framebuffer.h>
#include <aeos/vfs.h>
#include <aeos/ramfs.h>
#include <aeos/fs_persist.h>

/* Shell prompt */
#define PROMPT "AEOS> "

/* Current working directory */
static char cwd[256] = "/";

/* Forward declarations for built-in commands */
static int cmd_help(int argc, char **argv);
static int cmd_clear(int argc, char **argv);
static int cmd_echo(int argc, char **argv);
static int cmd_ps(int argc, char **argv);
static int cmd_meminfo(int argc, char **argv);
static int cmd_ls(int argc, char **argv);
static int cmd_cat(int argc, char **argv);
static int cmd_touch(int argc, char **argv);
static int cmd_mkdir(int argc, char **argv);
static int cmd_cd(int argc, char **argv);
static int cmd_pwd(int argc, char **argv);
static int cmd_rm(int argc, char **argv);
static int cmd_cp(int argc, char **argv);
static int cmd_mv(int argc, char **argv);
static int cmd_save(int argc, char **argv);
static int cmd_uname(int argc, char **argv);
static int cmd_exit(int argc, char **argv);

/* Built-in command table */
typedef struct {
    const char *name;
    int (*func)(int argc, char **argv);
    const char *description;
} shell_cmd_t;

static const shell_cmd_t builtin_commands[] = {
    {"help",    cmd_help,    "Show this help message"},
    {"clear",   cmd_clear,   "Clear the screen"},
    {"echo",    cmd_echo,    "Print text to console"},
    {"ps",      cmd_ps,      "List running processes"},
    {"meminfo", cmd_meminfo, "Display memory information"},
    {"ls",      cmd_ls,      "List files in directory"},
    {"cat",     cmd_cat,     "Display file contents"},
    {"touch",   cmd_touch,   "Create empty file"},
    {"mkdir",   cmd_mkdir,   "Create directory"},
    {"cd",      cmd_cd,      "Change directory"},
    {"pwd",     cmd_pwd,     "Print working directory"},
    {"rm",      cmd_rm,      "Remove file or directory"},
    {"cp",      cmd_cp,      "Copy file"},
    {"mv",      cmd_mv,      "Move/rename file"},
    {"save",    cmd_save,    "Save filesystem to persistent storage"},
    {"uname",   cmd_uname,   "Show system information"},
    {"exit",    cmd_exit,    "Exit the shell"},
    {NULL,      NULL,        NULL}
};

/**
 * Initialize shell
 */
void shell_init(void)
{
    klog_info("Shell subsystem initialized");
}

/**
 * Print shell banner
 */
static void print_banner(void)
{
    kprintf("\n");
    kprintf("╔════════════════════════════════════════╗\n");
    kprintf("║   AEOS Interactive Shell v1.0          ║\n");
    kprintf("║   Abdalla's Educational OS             ║\n");
    kprintf("╚════════════════════════════════════════╝\n");
    kprintf("\n");
    kprintf("Type 'help' for available commands\n");
    kprintf("\n");
}

/**
 * Resolve a path relative to current working directory
 * Returns pointer to static buffer containing absolute path
 */
static const char *resolve_path(const char *path)
{
    static char resolved[512];

    /* If path is NULL or empty, use current directory */
    if (path == NULL || path[0] == '\0') {
        return cwd;
    }

    /* If path is already absolute, return as-is */
    if (path[0] == '/') {
        return path;
    }

    /* Combine cwd with relative path */
    if (strcmp(cwd, "/") == 0) {
        /* Root directory - don't add extra slash */
        resolved[0] = '/';
        resolved[1] = '\0';
        strncat(resolved, path, sizeof(resolved) - 2);
    } else {
        /* Non-root directory - add slash separator */
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

/**
 * Read a line with backspace support
 */
int shell_readline(char *buf, int len)
{
    int pos = 0;
    char c;

    while (1) {
        c = uart_getc();

        if (c == '\r' || c == '\n') {
            /* End of line */
            uart_putc('\n');
            buf[pos] = '\0';
            return pos;
        } else if (c == '\b' || c == 0x7F) {
            /* Backspace */
            if (pos > 0) {
                pos--;
                uart_putc('\b');
                uart_putc(' ');
                uart_putc('\b');
            }
        } else if (c >= 32 && c < 127) {
            /* Printable character */
            if (pos < len - 1) {
                buf[pos++] = c;
                uart_putc(c);
            }
        }
        /* Ignore other control characters */
    }
}

/**
 * Parse command line into argc/argv
 */
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

/**
 * Execute a built-in command
 */
int shell_execute(int argc, char **argv)
{
    int i;

    if (argc == 0) {
        return 0;
    }

    /* Look for built-in command */
    for (i = 0; builtin_commands[i].name != NULL; i++) {
        if (strcmp(argv[0], builtin_commands[i].name) == 0) {
            return builtin_commands[i].func(argc, argv);
        }
    }

    /* Command not found */
    kprintf("%s: command not found\n", argv[0]);
    kprintf("Type 'help' for available commands\n");
    return -1;
}

/**
 * Main shell loop
 */
void shell_run(void)
{
    char line[SHELL_MAX_LINE];
    char *argv[SHELL_MAX_ARGS];
    int argc;

    print_banner();

    while (1) {
        /* Print prompt */
        kprintf(PROMPT);

        /* Read command */
        shell_readline(line, SHELL_MAX_LINE);

        /* Parse command */
        shell_parse(line, &argc, argv);

        /* Execute command */
        if (argc > 0) {
            shell_execute(argc, argv);
        }
    }
}

/* ========================================================================== */
/* Built-in Commands                                                         */
/* ========================================================================== */

/**
 * help - Show available commands
 */
static int cmd_help(int argc, char **argv)
{
    int i;
    (void)argc;
    (void)argv;

    kprintf("\nAvailable commands:\n\n");

    for (i = 0; builtin_commands[i].name != NULL; i++) {
        kprintf("  %-10s - %s\n",
                builtin_commands[i].name,
                builtin_commands[i].description);
    }

    kprintf("\n");
    return 0;
}

/**
 * clear - Clear screen
 */
static int cmd_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* ANSI escape code to clear screen */
    kprintf("\033[2J\033[H");
    return 0;
}

/**
 * echo - Print arguments
 */
static int cmd_echo(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        kprintf("%s", argv[i]);
        if (i < argc - 1) {
            kprintf(" ");
        }
    }
    kprintf("\n");

    return 0;
}

/**
 * ps - List processes
 */
static int cmd_ps(int argc, char **argv)
{
    scheduler_stats_t stats;
    (void)argc;
    (void)argv;

    scheduler_get_stats(&stats);

    kprintf("\nProcess Information:\n");
    kprintf("  Total processes:   %u\n", stats.total_processes);
    kprintf("  Running processes: %u\n", stats.running_processes);
    kprintf("  Context switches:  %llu\n", stats.context_switches);
    kprintf("\n");

    return 0;
}

/**
 * meminfo - Show memory information
 */
static int cmd_meminfo(int argc, char **argv)
{
    pmm_stats_t pmm_stats;
    heap_stats_t heap_stats;
    (void)argc;
    (void)argv;

    pmm_get_stats(&pmm_stats);
    heap_get_stats(&heap_stats);

    kprintf("\nMemory Information:\n\n");

    kprintf("Physical Memory (PMM):\n");
    kprintf("  Total pages:  %u (%u MB)\n",
            pmm_stats.total_pages,
            pmm_stats.total_pages * 4 / 1024);
    kprintf("  Used pages:   %u (%u MB)\n",
            pmm_stats.used_pages,
            pmm_stats.used_pages * 4 / 1024);
    kprintf("  Free pages:   %u (%u MB)\n",
            pmm_stats.free_pages,
            pmm_stats.free_pages * 4 / 1024);

    kprintf("\nKernel Heap:\n");
    kprintf("  Total size:   %u KB\n", heap_stats.total_size / 1024);
    kprintf("  Used:         %u bytes\n", heap_stats.used_size);
    kprintf("  Free:         %u KB\n", heap_stats.free_size / 1024);
    kprintf("  Allocations:  %u\n", heap_stats.num_allocs);
    kprintf("  Frees:        %u\n", heap_stats.num_frees);

    kprintf("\n");
    return 0;
}

/**
 * ls - List files in current directory
 */
static int cmd_ls(int argc, char **argv)
{
    const char *path;
    int fd;
    vfs_dirent_t entry;
    int ret;

    /* Resolve path - default to cwd if no argument */
    path = (argc > 1) ? resolve_path(argv[1]) : cwd;

    /* Open directory */
    fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0) {
        kprintf("ls: cannot access '%s': No such file or directory\n", path);
        return -1;
    }

    kprintf("\nListing: %s\n", path);
    kprintf("------------------------------------------------------\n");

    /* Read directory entries */
    while (1) {
        ret = vfs_readdir(fd, &entry);
        if (ret < 0) {
            break;
        }

        klog_debug("ls: got entry name='%s' type=%d size=%llu",
                   entry.name, entry.type, entry.size);

        /* Print entry */
        if (entry.type == VFS_FILE_DIRECTORY) {
            klog_debug("ls: printing directory entry");
            kprintf("  [DIR]  %s/\n", entry.name);
            klog_debug("ls: directory entry printed");
        } else {
            klog_debug("ls: printing file entry");
            kprintf("  [FILE] %s (%u bytes)\n", entry.name, (uint32_t)entry.size);
            klog_debug("ls: file entry printed");
        }
    }

    kprintf("------------------------------------------------------\n\n");

    vfs_close(fd);
    return 0;
}

/**
 * cat - Display file contents
 */
static int cmd_cat(int argc, char **argv)
{
    int fd;
    char buffer[256];
    ssize_t bytes_read;
    const char *path;

    if (argc < 2) {
        kprintf("Usage: cat <filename>\n");
        return -1;
    }

    /* Resolve path relative to cwd */
    path = resolve_path(argv[1]);

    /* Open file */
    fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0) {
        kprintf("cat: %s: No such file or directory\n", argv[1]);
        return -1;
    }

    /* Read and print file contents */
    kprintf("\n");
    while (1) {
        bytes_read = vfs_read(fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            break;
        }

        buffer[bytes_read] = '\0';
        kprintf("%s", buffer);
    }
    kprintf("\n");

    vfs_close(fd);
    return 0;
}

/**
 * touch - Create empty file
 */
static int cmd_touch(int argc, char **argv)
{
    int fd;
    const char *path;

    if (argc < 2) {
        kprintf("Usage: touch <filename>\n");
        return -1;
    }

    /* Resolve path relative to cwd */
    path = resolve_path(argv[1]);

    /* Create file */
    fd = vfs_open(path, O_CREAT | O_WRONLY, 0644);
    if (fd < 0) {
        kprintf("touch: cannot create '%s': Permission denied or path not found\n", argv[1]);
        return -1;
    }

    kprintf("Created file: %s\n", argv[1]);
    vfs_close(fd);
    return 0;
}

/**
 * mkdir - Create directory
 */
static int cmd_mkdir(int argc, char **argv)
{
    int ret;
    const char *path;

    if (argc < 2) {
        kprintf("Usage: mkdir <directory>\n");
        return -1;
    }

    /* Resolve path relative to cwd */
    path = resolve_path(argv[1]);

    /* Create directory */
    ret = vfs_mkdir(path, 0755);
    if (ret < 0) {
        kprintf("mkdir: cannot create directory '%s'\n", argv[1]);
        return -1;
    }

    kprintf("Created directory: %s\n", argv[1]);
    return 0;
}

/**
 * cd - Change directory
 */
static int cmd_cd(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/";
    int fd;
    char new_cwd[256];

    /* Handle special case: ".." (parent directory) */
    if (strcmp(path, "..") == 0) {
        /* Find the last '/' in cwd and truncate there */
        size_t len = strlen(cwd);
        if (len > 1) {
            /* Start from end and find last '/' */
            char *last_slash = NULL;
            for (size_t i = len - 1; i > 0; i--) {
                if (cwd[i] == '/') {
                    last_slash = &cwd[i];
                    break;
                }
            }
            if (last_slash != NULL) {
                if (last_slash == cwd) {
                    /* Parent is root */
                    strcpy(cwd, "/");
                } else {
                    /* Truncate at last slash */
                    *last_slash = '\0';
                }
            } else {
                /* No slash found, go to root */
                strcpy(cwd, "/");
            }
        }
        /* Already at root, stay there */
        return 0;
    }

    /* Handle special case: "." (current directory) */
    if (strcmp(path, ".") == 0) {
        /* Stay in current directory */
        return 0;
    }

    /* Build absolute path */
    if (path[0] == '/') {
        /* Absolute path */
        strncpy(new_cwd, path, sizeof(new_cwd) - 1);
        new_cwd[sizeof(new_cwd) - 1] = '\0';
    } else {
        /* Relative path - append to current directory */
        if (strcmp(cwd, "/") == 0) {
            new_cwd[0] = '/';
            strncpy(new_cwd + 1, path, sizeof(new_cwd) - 2);
            new_cwd[sizeof(new_cwd) - 1] = '\0';
        } else {
            strncpy(new_cwd, cwd, sizeof(new_cwd) - 1);
            new_cwd[sizeof(new_cwd) - 1] = '\0';
            size_t cwd_len = strlen(new_cwd);
            if (cwd_len < sizeof(new_cwd) - 1) {
                new_cwd[cwd_len] = '/';
                strncpy(new_cwd + cwd_len + 1, path, sizeof(new_cwd) - cwd_len - 2);
                new_cwd[sizeof(new_cwd) - 1] = '\0';
            }
        }
    }

    /* Try to open the directory to verify it exists and is a directory */
    fd = vfs_open(new_cwd, O_RDONLY, 0);
    if (fd < 0) {
        kprintf("cd: %s: No such file or directory\n", path);
        return -1;
    }

    /* TODO: Check if it's actually a directory (currently trusting vfs_open) */
    vfs_close(fd);

    /* Update current working directory */
    strncpy(cwd, new_cwd, sizeof(cwd) - 1);
    cwd[sizeof(cwd) - 1] = '\0';

    /* Normalize path (remove trailing slash unless it's root) */
    size_t len = strlen(cwd);
    if (len > 1 && cwd[len - 1] == '/') {
        cwd[len - 1] = '\0';
    }

    return 0;
}

/**
 * pwd - Print working directory
 */
static int cmd_pwd(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    kprintf("%s\n", cwd);
    return 0;
}

/**
 * rm - Remove file or directory
 */
static int cmd_rm(int argc, char **argv)
{
    int ret;
    int arg_idx = 1;
    int recursive = 0;
    int force = 0;

    if (argc < 2) {
        kprintf("Usage: rm [-rf] <file|directory>\n");
        return -1;
    }

    /* Parse flags */
    while (arg_idx < argc && argv[arg_idx][0] == '-') {
        char *flag = argv[arg_idx];

        /* Skip the '-' */
        flag++;

        /* Parse each character in the flag */
        while (*flag != '\0') {
            if (*flag == 'r') {
                recursive = 1;
            } else if (*flag == 'f') {
                force = 1;
            } else {
                kprintf("rm: invalid option -- '%c'\n", *flag);
                kprintf("Usage: rm [-rf] <file|directory>\n");
                return -1;
            }
            flag++;
        }

        arg_idx++;
    }

    /* Check if we have a file/directory argument */
    if (arg_idx >= argc) {
        kprintf("Usage: rm [-rf] <file|directory>\n");
        return -1;
    }

    /* Get the file/directory to remove and resolve path */
    const char *target = resolve_path(argv[arg_idx]);

    /* Try to remove as file first */
    ret = vfs_unlink(target);
    if (ret < 0) {
        /* If unlink failed, try rmdir if recursive flag is set */
        if (recursive) {
            ret = vfs_rmdir(target);
            if (ret < 0 && !force) {
                kprintf("rm: cannot remove '%s': Directory not empty or no such file\n", target);
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

/**
 * cp - Copy file
 */
static int cmd_cp(int argc, char **argv)
{
    int src_fd, dst_fd;
    char buffer[512];
    ssize_t bytes_read, bytes_written;
    const char *src_path, *dst_path;

    if (argc < 3) {
        kprintf("Usage: cp <source> <destination>\n");
        return -1;
    }

    /* Resolve paths relative to cwd */
    src_path = resolve_path(argv[1]);
    dst_path = resolve_path(argv[2]);

    /* Open source file */
    src_fd = vfs_open(src_path, O_RDONLY, 0);
    if (src_fd < 0) {
        kprintf("cp: cannot open '%s': No such file\n", argv[1]);
        return -1;
    }

    /* Create destination file */
    dst_fd = vfs_open(dst_path, O_CREAT | O_WRONLY, 0644);
    if (dst_fd < 0) {
        kprintf("cp: cannot create '%s'\n", argv[2]);
        vfs_close(src_fd);
        return -1;
    }

    /* Copy data */
    while (1) {
        bytes_read = vfs_read(src_fd, buffer, sizeof(buffer));
        if (bytes_read <= 0) {
            break;
        }

        bytes_written = vfs_write(dst_fd, buffer, bytes_read);
        if (bytes_written < 0 || bytes_written != bytes_read) {
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

/**
 * mv - Move/rename file
 */
static int cmd_mv(int argc, char **argv)
{
    /* For now, implement as copy + delete */
    int ret;
    const char *src_path;

    if (argc < 3) {
        kprintf("Usage: mv <source> <destination>\n");
        return -1;
    }

    /* Copy file */
    char *cp_argv[] = {"cp", argv[1], argv[2]};
    ret = cmd_cp(3, cp_argv);
    if (ret < 0) {
        return -1;
    }

    /* Delete source - resolve path */
    src_path = resolve_path(argv[1]);
    ret = vfs_unlink(src_path);
    if (ret < 0) {
        kprintf("mv: cannot remove '%s' after copy\n", argv[1]);
        return -1;
    }

    kprintf("Moved: %s -> %s\n", argv[1], argv[2]);
    return 0;
}

/**
 * save - Save filesystem to persistent storage
 */
static int cmd_save(int argc, char **argv)
{
    vfs_filesystem_t *fs;
    int ret;

    (void)argc;
    (void)argv;

    kprintf("\nSaving filesystem to disk...\n");

    /* Get root filesystem */
    fs = vfs_get_root_fs();
    if (fs == NULL) {
        kprintf("[ERROR] Failed to get root filesystem\n");
        return -1;
    }

    /* Save filesystem to disk */
    ret = fs_save_to_disk(fs);
    if (ret < 0) {
        kprintf("[ERROR] Failed to save filesystem to disk\n");
        return -1;
    }

    kprintf("Filesystem saved successfully!\n");
    kprintf("Changes will persist across reboots (stored on disk.img)\n\n");

    return 0;
}

/**
 * uname - Show system info
 */
static int cmd_uname(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    kprintf("\nAEOS (Abdalla's Educational Operating System)\n");
    kprintf("Version: 1.0.0\n");
    kprintf("Architecture: AArch64 (ARMv8-A)\n");
    kprintf("Platform: QEMU virt (Cortex-A57)\n");
    kprintf("\n");

    return 0;
}

/**
 * exit - Exit shell
 */
static int cmd_exit(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    kprintf("\nExiting shell...\n");
    kprintf("System halted.\n");
    kprintf("\nPress Ctrl+A then X to exit QEMU\n");

    /* Halt in infinite loop */
    while (1) {
        __asm__ volatile("wfi");
    }

    return 0;
}

/* ============================================================================
 * End of shell.c
 * ============================================================================ */
