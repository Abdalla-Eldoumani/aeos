/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/kernel/shell.c
 * Description: Interactive shell implementation with history and tab completion
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
#include <aeos/timer.h>
#include <aeos/editor.h>

/* External symbols from vectors.asm */
extern uint64_t exception_counters[16];

/* ANSI escape color codes for terminal output */
#define ANSI_RESET     "\033[0m"
#define ANSI_RED       "\033[31m"
#define ANSI_GREEN     "\033[32m"
#define ANSI_YELLOW    "\033[33m"
#define ANSI_BLUE      "\033[34m"
#define ANSI_MAGENTA   "\033[35m"
#define ANSI_CYAN      "\033[36m"
#define ANSI_BOLD      "\033[1m"

/* Shell prompt with color */
#define PROMPT ANSI_GREEN "AEOS" ANSI_RESET "> "
#define PROMPT_LEN 6  /* Visible length without escape codes */

/* Command history settings */
#define HISTORY_SIZE    32
#define HISTORY_LINE_LEN SHELL_MAX_LINE

/* Command history circular buffer */
static char history[HISTORY_SIZE][HISTORY_LINE_LEN];
static int history_count = 0;      /* Total commands ever entered */
static int history_start = 0;      /* Oldest entry index */

/* Extended key codes for escape sequences (shell-specific, avoid editor.h conflicts) */
#define SHELL_KEY_UP          2000
#define SHELL_KEY_DOWN        2001
#define SHELL_KEY_RIGHT       2002
#define SHELL_KEY_LEFT        2003
#define SHELL_KEY_HOME        2004
#define SHELL_KEY_END         2005
#define SHELL_KEY_DELETE      2006

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
static int cmd_uptime(int argc, char **argv);
static int cmd_irqinfo(int argc, char **argv);
static int cmd_edit(int argc, char **argv);
static int cmd_history(int argc, char **argv);
static int cmd_time(int argc, char **argv);
static int cmd_hexdump(int argc, char **argv);
static int cmd_write(int argc, char **argv);
static int cmd_grep(int argc, char **argv);
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
    {"uptime",  cmd_uptime,  "Show system uptime"},
    {"irqinfo", cmd_irqinfo, "Show interrupt statistics"},
    {"edit",    cmd_edit,    "Edit file (vim-like editor)"},
    {"vi",      cmd_edit,    "Edit file (alias for edit)"},
    {"history", cmd_history, "Show command history"},
    {"time",    cmd_time,    "Time command execution"},
    {"hexdump", cmd_hexdump, "Hex dump of file contents"},
    {"write",   cmd_write,   "Write text to file"},
    {"grep",    cmd_grep,    "Search for pattern in file"},
    {"exit",    cmd_exit,    "Exit the shell"},
    {NULL,      NULL,        NULL}
};

/**
 * Add command to history
 */
static void history_add(const char *cmd)
{
    int idx;

    /* Don't add empty commands or duplicates of last command */
    if (cmd[0] == '\0') {
        return;
    }

    /* Check if same as last command */
    if (history_count > 0) {
        int last_idx = (history_start + history_count - 1) % HISTORY_SIZE;
        if (strcmp(history[last_idx], cmd) == 0) {
            return;
        }
    }

    /* Add to circular buffer */
    if (history_count < HISTORY_SIZE) {
        idx = history_count;
        history_count++;
    } else {
        /* Buffer full, overwrite oldest */
        idx = history_start;
        history_start = (history_start + 1) % HISTORY_SIZE;
    }

    strncpy(history[idx], cmd, HISTORY_LINE_LEN - 1);
    history[idx][HISTORY_LINE_LEN - 1] = '\0';
}

/**
 * Get history entry by relative index (0 = most recent)
 */
static const char *history_get(int rel_idx)
{
    int count = (history_count < HISTORY_SIZE) ? history_count : HISTORY_SIZE;

    if (rel_idx < 0 || rel_idx >= count) {
        return NULL;
    }

    int idx = (history_start + count - 1 - rel_idx) % HISTORY_SIZE;
    return history[idx];
}

/* Advanced shell features (temporarily disabled for debugging) */
#if 0
/**
 * Read a key with escape sequence handling
 * Returns ASCII for regular keys, SHELL_KEY_* for special keys
 *
 * Uses blocking reads with simple timeout via busy-wait counter.
 * This approach works reliably on QEMU's UART.
 */
static int shell_read_key(void)
{
    char c = uart_getc();

    if (c != 27) {
        return c;  /* Regular character */
    }

    /* Escape sequence detected - check for more characters */
    /* Use a simple delay-based approach instead of uart_data_available */
    /* This works more reliably on QEMU */
    volatile int i;
    for (i = 0; i < 50000; i++) {
        __asm__ volatile("nop");
    }

    /* Try to read next character (non-blocking check via flag register) */
    uint32_t fr = *(volatile uint32_t *)(0x09000000 + 0x18);  /* UART_FR */
    if (fr & (1 << 4)) {  /* RXFE - RX FIFO empty */
        return 27;  /* Just ESC key, no sequence */
    }

    char seq1 = uart_getc();
    if (seq1 != '[') {
        return 27;  /* Unknown sequence */
    }

    /* Wait and check for next char */
    for (i = 0; i < 50000; i++) {
        __asm__ volatile("nop");
    }

    fr = *(volatile uint32_t *)(0x09000000 + 0x18);
    if (fr & (1 << 4)) {
        return 27;
    }

    char seq2 = uart_getc();

    switch (seq2) {
        case 'A': return SHELL_KEY_UP;
        case 'B': return SHELL_KEY_DOWN;
        case 'C': return SHELL_KEY_RIGHT;
        case 'D': return SHELL_KEY_LEFT;
        case 'H': return SHELL_KEY_HOME;
        case 'F': return SHELL_KEY_END;
        case '3':
            /* Delete key: ESC [ 3 ~ */
            for (i = 0; i < 50000; i++) {
                __asm__ volatile("nop");
            }
            fr = *(volatile uint32_t *)(0x09000000 + 0x18);
            if (!(fr & (1 << 4))) {
                char seq3 = uart_getc();
                if (seq3 == '~') {
                    return SHELL_KEY_DELETE;
                }
            }
            return 27;
        case '1':
            /* Home: ESC [ 1 ~ */
            for (i = 0; i < 50000; i++) {
                __asm__ volatile("nop");
            }
            fr = *(volatile uint32_t *)(0x09000000 + 0x18);
            if (!(fr & (1 << 4))) {
                char seq3 = uart_getc();
                if (seq3 == '~') {
                    return SHELL_KEY_HOME;
                }
            }
            return 27;
        case '4':
            /* End: ESC [ 4 ~ */
            for (i = 0; i < 50000; i++) {
                __asm__ volatile("nop");
            }
            fr = *(volatile uint32_t *)(0x09000000 + 0x18);
            if (!(fr & (1 << 4))) {
                char seq3 = uart_getc();
                if (seq3 == '~') {
                    return SHELL_KEY_END;
                }
            }
            return 27;
        default:
            return 27;
    }
}

/**
 * Clear current line and reprint
 */
static void shell_redraw_line(const char *buf, int len, int cursor)
{
    int i;

    /* Move to start of line (after prompt) and clear to end */
    kprintf("\r" PROMPT);
    kprintf("\033[K");  /* Clear to end of line */

    /* Print buffer */
    for (i = 0; i < len; i++) {
        uart_putc(buf[i]);
    }

    /* Move cursor to correct position */
    if (cursor < len) {
        /* Move cursor back */
        for (i = 0; i < (len - cursor); i++) {
            kprintf("\033[D");
        }
    }
}

/**
 * Try to complete command or filename
 * Returns number of matches found
 */
static int shell_tab_complete(char *buf, int *len, int *cursor)
{
    int i;
    int word_start = 0;
    int match_count = 0;
    const char *first_match = NULL;
    int first_match_len = 0;
    int word_len;

    /* Find start of current word */
    for (i = *cursor - 1; i >= 0; i--) {
        if (buf[i] == ' ' || buf[i] == '\t') {
            break;
        }
    }
    word_start = i + 1;
    word_len = *cursor - word_start;

    /* Determine if we're completing a command or filename */
    int is_first_word = 1;
    for (i = 0; i < word_start; i++) {
        if (buf[i] != ' ' && buf[i] != '\t') {
            is_first_word = 0;
            break;
        }
    }

    if (is_first_word) {
        /* Complete command name */
        kprintf("\n");

        for (i = 0; builtin_commands[i].name != NULL; i++) {
            if (word_len == 0 || strncmp(builtin_commands[i].name, buf + word_start, word_len) == 0) {
                match_count++;
                if (match_count == 1) {
                    first_match = builtin_commands[i].name;
                    first_match_len = strlen(first_match);
                }
                kprintf("  %s", builtin_commands[i].name);
            }
        }

        kprintf("\n");

        /* If exactly one match, complete it */
        if (match_count == 1 && first_match != NULL) {
            /* Replace current word with match */
            int to_add = first_match_len - word_len;
            if (*len + to_add < SHELL_MAX_LINE - 1) {
                /* Copy remaining part of match */
                for (i = 0; i < to_add; i++) {
                    buf[*len + i] = first_match[word_len + i];
                }
                *len += to_add;
                *cursor = *len;
                buf[*len] = '\0';

                /* Add space after completion */
                if (*len < SHELL_MAX_LINE - 1) {
                    buf[*len] = ' ';
                    (*len)++;
                    (*cursor)++;
                    buf[*len] = '\0';
                }
            }
        }

        /* Redraw prompt and line */
        kprintf(PROMPT);
        for (i = 0; i < *len; i++) {
            uart_putc(buf[i]);
        }
    } else {
        /* Complete filename - list files in current directory */
        const char *partial = buf + word_start;
        int fd;
        vfs_dirent_t entry;

        kprintf("\n");

        fd = vfs_open(cwd, O_RDONLY, 0);
        if (fd >= 0) {
            while (vfs_readdir(fd, &entry) >= 0) {
                if (word_len == 0 || strncmp(entry.name, partial, word_len) == 0) {
                    match_count++;
                    if (match_count == 1) {
                        /* Store first match - static buffer */
                        static char match_buf[256];
                        strncpy(match_buf, entry.name, sizeof(match_buf) - 1);
                        match_buf[sizeof(match_buf) - 1] = '\0';
                        first_match = match_buf;
                        first_match_len = strlen(first_match);
                    }

                    if (entry.type == VFS_FILE_DIRECTORY) {
                        kprintf("  " ANSI_BLUE "%s/" ANSI_RESET, entry.name);
                    } else {
                        kprintf("  %s", entry.name);
                    }
                }
            }
            vfs_close(fd);
        }

        kprintf("\n");

        /* If exactly one match, complete it */
        if (match_count == 1 && first_match != NULL) {
            int to_add = first_match_len - word_len;
            if (*len + to_add < SHELL_MAX_LINE - 1) {
                for (i = 0; i < to_add; i++) {
                    buf[*len + i] = first_match[word_len + i];
                }
                *len += to_add;
                *cursor = *len;
                buf[*len] = '\0';
            }
        }

        /* Redraw prompt and line */
        kprintf(PROMPT);
        for (i = 0; i < *len; i++) {
            uart_putc(buf[i]);
        }
    }

    return match_count;
}
#endif /* Advanced shell features */

/**
 * Initialize shell
 */
void shell_init(void)
{
    /* Clear history */
    history_count = 0;
    history_start = 0;

    klog_info("Shell subsystem initialized (with history and tab completion)");
}

/**
 * Print shell banner
 */
static void print_banner(void)
{
    kprintf("\n");
    kprintf(ANSI_CYAN "╔════════════════════════════════════════════════════╗\n");
    kprintf("║   " ANSI_GREEN "AEOS Interactive Shell v2.0" ANSI_CYAN "                    ║\n");
    kprintf("║   " ANSI_RESET "Abdalla's Educational Operating System" ANSI_CYAN "         ║\n");
    kprintf("╚════════════════════════════════════════════════════╝" ANSI_RESET "\n");
    kprintf("\n");
    kprintf("Type " ANSI_YELLOW "'help'" ANSI_RESET " for commands, ");
    kprintf(ANSI_YELLOW "'history'" ANSI_RESET " to see command history\n");
    kprintf("Use " ANSI_YELLOW "'edit'" ANSI_RESET " or " ANSI_YELLOW "'vi'" ANSI_RESET " to open the text editor\n");
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
 * Read a line with basic editing support
 * Simple version that just uses blocking UART reads
 */
int shell_readline(char *buf, int maxlen)
{
    int pos = 0;
    char c;

    while (1) {
        c = uart_getc();

        if (c == '\r' || c == '\n') {
            /* End of line */
            uart_putc('\n');
            buf[pos] = '\0';

            /* Add to history if non-empty */
            if (pos > 0) {
                history_add(buf);
            }

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
            if (pos < maxlen - 1) {
                buf[pos++] = c;
                uart_putc(c);
            }
        }
        /* Ignore other control characters including escape sequences */
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
    (void)argc;
    (void)argv;

    kprintf("\n" ANSI_CYAN "Available commands:" ANSI_RESET "\n\n");

    kprintf(ANSI_YELLOW "File Operations:" ANSI_RESET "\n");
    kprintf("  " ANSI_GREEN "ls" ANSI_RESET "        - List files in directory\n");
    kprintf("  " ANSI_GREEN "cat" ANSI_RESET "       - Display file contents\n");
    kprintf("  " ANSI_GREEN "touch" ANSI_RESET "     - Create empty file\n");
    kprintf("  " ANSI_GREEN "mkdir" ANSI_RESET "     - Create directory\n");
    kprintf("  " ANSI_GREEN "rm" ANSI_RESET "        - Remove file or directory\n");
    kprintf("  " ANSI_GREEN "cp" ANSI_RESET "        - Copy file\n");
    kprintf("  " ANSI_GREEN "mv" ANSI_RESET "        - Move/rename file\n");
    kprintf("  " ANSI_GREEN "cd" ANSI_RESET "        - Change directory\n");
    kprintf("  " ANSI_GREEN "pwd" ANSI_RESET "       - Print working directory\n");
    kprintf("  " ANSI_GREEN "write" ANSI_RESET "     - Write text to file\n");
    kprintf("  " ANSI_GREEN "hexdump" ANSI_RESET "   - Hex dump of file contents\n");
    kprintf("  " ANSI_GREEN "grep" ANSI_RESET "      - Search for pattern in file\n");

    kprintf("\n" ANSI_YELLOW "Editor:" ANSI_RESET "\n");
    kprintf("  " ANSI_GREEN "edit" ANSI_RESET "/" ANSI_GREEN "vi" ANSI_RESET "  - Vim-like text editor\n");

    kprintf("\n" ANSI_YELLOW "System Information:" ANSI_RESET "\n");
    kprintf("  " ANSI_GREEN "ps" ANSI_RESET "        - List running processes\n");
    kprintf("  " ANSI_GREEN "meminfo" ANSI_RESET "   - Display memory information\n");
    kprintf("  " ANSI_GREEN "uptime" ANSI_RESET "    - Show system uptime\n");
    kprintf("  " ANSI_GREEN "irqinfo" ANSI_RESET "   - Show interrupt statistics\n");
    kprintf("  " ANSI_GREEN "uname" ANSI_RESET "     - Show system information\n");

    kprintf("\n" ANSI_YELLOW "Shell Utilities:" ANSI_RESET "\n");
    kprintf("  " ANSI_GREEN "echo" ANSI_RESET "      - Print text to console\n");
    kprintf("  " ANSI_GREEN "clear" ANSI_RESET "     - Clear the screen\n");
    kprintf("  " ANSI_GREEN "history" ANSI_RESET "   - Show command history\n");
    kprintf("  " ANSI_GREEN "time" ANSI_RESET "      - Time command execution\n");
    kprintf("  " ANSI_GREEN "help" ANSI_RESET "      - Show this help message\n");

    kprintf("\n" ANSI_YELLOW "Persistence:" ANSI_RESET "\n");
    kprintf("  " ANSI_GREEN "save" ANSI_RESET "      - Save filesystem to host\n");
    kprintf("  " ANSI_GREEN "exit" ANSI_RESET "      - Exit the shell\n");

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

        /* Print entry with color */
        if (entry.type == VFS_FILE_DIRECTORY) {
            klog_debug("ls: printing directory entry");
            kprintf("  " ANSI_BLUE "[DIR]" ANSI_RESET "  " ANSI_BLUE "%s/" ANSI_RESET "\n", entry.name);
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
 * uptime - Show system uptime
 */
static int cmd_uptime(int argc, char **argv)
{
    uint64_t uptime_sec = timer_get_uptime_sec();
    uint64_t ticks = timer_get_ticks();
    uint32_t hours, minutes, seconds;

    (void)argc;
    (void)argv;

    /* Calculate hours, minutes, seconds */
    hours = (uint32_t)(uptime_sec / 3600);
    minutes = (uint32_t)((uptime_sec % 3600) / 60);
    seconds = (uint32_t)(uptime_sec % 60);

    kprintf("\nSystem Uptime:\n");
    kprintf("  Time:  %u:%02u:%02u (hh:mm:ss)\n", hours, minutes, seconds);
    kprintf("  Ticks: %llu (at %u Hz)\n", ticks, TIMER_FREQ_HZ);
    kprintf("\n");

    return 0;
}

/**
 * irqinfo - Show interrupt statistics
 */
static int cmd_irqinfo(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    kprintf("\nInterrupt Statistics:\n\n");

    kprintf("Exception Vector Counters:\n");
    kprintf("  EL1 SP0: sync=%llu irq=%llu fiq=%llu serr=%llu\n",
            exception_counters[0], exception_counters[1],
            exception_counters[2], exception_counters[3]);
    kprintf("  EL1 SPx: sync=%llu irq=%llu fiq=%llu serr=%llu\n",
            exception_counters[4], exception_counters[5],
            exception_counters[6], exception_counters[7]);
    kprintf("  EL0 A64: sync=%llu irq=%llu fiq=%llu serr=%llu\n",
            exception_counters[8], exception_counters[9],
            exception_counters[10], exception_counters[11]);
    kprintf("  EL0 A32: sync=%llu irq=%llu fiq=%llu serr=%llu\n",
            exception_counters[12], exception_counters[13],
            exception_counters[14], exception_counters[15]);

    /* Calculate total IRQs */
    uint64_t total_irqs = exception_counters[1] + exception_counters[5] +
                          exception_counters[9] + exception_counters[13];
    kprintf("\n  Total IRQs handled: %llu\n", total_irqs);
    kprintf("\n");

    return 0;
}

/**
 * edit/vi - Edit a file with vim-like editor
 */
static int cmd_edit(int argc, char **argv)
{
    const char *path;

    if (argc < 2) {
        kprintf("Usage: edit <filename>\n");
        kprintf("       vi <filename>\n");
        return -1;
    }

    /* Resolve path relative to cwd */
    path = resolve_path(argv[1]);

    /* Run the editor */
    editor_run(path);

    return 0;
}

/**
 * history - Show command history
 */
static int cmd_history(int argc, char **argv)
{
    int count = (history_count < HISTORY_SIZE) ? history_count : HISTORY_SIZE;
    int i;

    (void)argc;
    (void)argv;

    if (count == 0) {
        kprintf("No commands in history.\n");
        return 0;
    }

    kprintf("\nCommand History:\n");
    for (i = count - 1; i >= 0; i--) {
        const char *cmd = history_get(i);
        if (cmd != NULL) {
            kprintf("  %3d  %s\n", count - i, cmd);
        }
    }
    kprintf("\n");

    return 0;
}

/**
 * time - Time command execution
 */
static int cmd_time(int argc, char **argv)
{
    uint64_t start_ticks, end_ticks, elapsed_ticks;
    uint64_t elapsed_ms;

    if (argc < 2) {
        kprintf("Usage: time <command> [args...]\n");
        return -1;
    }

    /* Get start time */
    start_ticks = timer_get_ticks();

    /* Execute the command (shift argv to skip "time") */
    shell_execute(argc - 1, &argv[1]);

    /* Get end time */
    end_ticks = timer_get_ticks();

    /* Calculate elapsed time */
    elapsed_ticks = end_ticks - start_ticks;
    elapsed_ms = (elapsed_ticks * 1000) / TIMER_FREQ_HZ;

    kprintf("\n" ANSI_CYAN "Time:" ANSI_RESET " %llu ms (%llu ticks)\n", elapsed_ms, elapsed_ticks);

    return 0;
}

/**
 * hexdump - Hex dump of file contents
 */
static int cmd_hexdump(int argc, char **argv)
{
    int fd;
    unsigned char buffer[16];
    ssize_t bytes_read;
    size_t offset = 0;
    const char *path;
    int i;

    if (argc < 2) {
        kprintf("Usage: hexdump <filename>\n");
        return -1;
    }

    path = resolve_path(argv[1]);

    fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0) {
        kprintf(ANSI_RED "hexdump: cannot open '%s': No such file" ANSI_RESET "\n", argv[1]);
        return -1;
    }

    kprintf("\nHex dump of %s:\n", argv[1]);
    kprintf(ANSI_CYAN "Offset    00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  ASCII" ANSI_RESET "\n");
    kprintf("--------  -----------------------------------------------  ----------------\n");

    while (1) {
        bytes_read = vfs_read(fd, buffer, 16);
        if (bytes_read <= 0) {
            break;
        }

        /* Print offset */
        kprintf("%08x  ", (unsigned int)offset);

        /* Print hex values */
        for (i = 0; i < 16; i++) {
            if (i == 8) {
                kprintf(" ");  /* Extra space in middle */
            }
            if (i < bytes_read) {
                kprintf("%02x ", buffer[i]);
            } else {
                kprintf("   ");
            }
        }

        /* Print ASCII representation */
        kprintf(" ");
        for (i = 0; i < bytes_read; i++) {
            if (buffer[i] >= 32 && buffer[i] < 127) {
                kprintf("%c", buffer[i]);
            } else {
                kprintf(".");
            }
        }
        kprintf("\n");

        offset += bytes_read;

        /* Limit output to prevent flooding */
        if (offset >= 512) {
            kprintf("... (truncated at 512 bytes, file may be larger)\n");
            break;
        }
    }

    kprintf("\nTotal: %u bytes\n\n", (unsigned int)offset);

    vfs_close(fd);
    return 0;
}

/**
 * write - Write text to file
 */
static int cmd_write(int argc, char **argv)
{
    int fd;
    const char *path;
    int i;

    if (argc < 3) {
        kprintf("Usage: write <filename> <text...>\n");
        kprintf("       Creates file and writes text to it\n");
        return -1;
    }

    path = resolve_path(argv[1]);

    fd = vfs_open(path, O_CREAT | O_WRONLY, 0644);
    if (fd < 0) {
        kprintf(ANSI_RED "write: cannot create '%s'" ANSI_RESET "\n", argv[1]);
        return -1;
    }

    /* Write all arguments after filename as text */
    for (i = 2; i < argc; i++) {
        vfs_write(fd, argv[i], strlen(argv[i]));
        if (i < argc - 1) {
            vfs_write(fd, " ", 1);
        }
    }
    vfs_write(fd, "\n", 1);

    vfs_close(fd);

    kprintf(ANSI_GREEN "Wrote to %s" ANSI_RESET "\n", argv[1]);
    return 0;
}

/**
 * grep - Search for pattern in file
 */
static int cmd_grep(int argc, char **argv)
{
    int fd;
    char buffer[512];
    char line[256];
    ssize_t bytes_read;
    const char *path;
    const char *pattern;
    int line_num = 0;
    int matches = 0;
    int line_pos = 0;
    int i;

    if (argc < 3) {
        kprintf("Usage: grep <pattern> <filename>\n");
        return -1;
    }

    pattern = argv[1];

    /* Strip leading/trailing quotes from pattern */
    {
        int plen = strlen(pattern);
        if (plen >= 2) {
            char first = pattern[0];
            char last = pattern[plen - 1];
            if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
                /* Create unquoted pattern in static buffer */
                static char unquoted[256];
                int i;
                for (i = 1; i < plen - 1 && i < 255; i++) {
                    unquoted[i - 1] = pattern[i];
                }
                unquoted[i - 1] = '\0';
                pattern = unquoted;
            }
        }
    }

    path = resolve_path(argv[2]);

    fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0) {
        kprintf(ANSI_RED "grep: cannot open '%s': No such file" ANSI_RESET "\n", argv[2]);
        return -1;
    }

    kprintf("\n");

    /* Read file and search line by line */
    while (1) {
        bytes_read = vfs_read(fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            break;
        }
        buffer[bytes_read] = '\0';

        /* Process buffer character by character */
        for (i = 0; i < bytes_read; i++) {
            if (buffer[i] == '\n' || buffer[i] == '\r') {
                /* End of line - check for pattern */
                line[line_pos] = '\0';
                line_num++;

                /* Simple substring search */
                if (line_pos > 0) {
                    char *found = NULL;
                    int j;
                    int pat_len = strlen(pattern);
                    int line_len = line_pos;

                    for (j = 0; j <= line_len - pat_len; j++) {
                        if (strncmp(&line[j], pattern, pat_len) == 0) {
                            found = &line[j];
                            break;
                        }
                    }

                    if (found != NULL) {
                        kprintf(ANSI_CYAN "%d:" ANSI_RESET " %s\n", line_num, line);
                        matches++;
                    }
                }

                line_pos = 0;
                /* Skip \r\n combination */
                if (buffer[i] == '\r' && i + 1 < bytes_read && buffer[i + 1] == '\n') {
                    i++;
                }
            } else {
                if (line_pos < (int)sizeof(line) - 1) {
                    line[line_pos++] = buffer[i];
                }
            }
        }
    }

    /* Check last line if no trailing newline */
    if (line_pos > 0) {
        line[line_pos] = '\0';
        line_num++;

        char *found = NULL;
        int j;
        int pat_len = strlen(pattern);

        for (j = 0; j <= line_pos - pat_len; j++) {
            if (strncmp(&line[j], pattern, pat_len) == 0) {
                found = &line[j];
                break;
            }
        }

        if (found != NULL) {
            kprintf(ANSI_CYAN "%d:" ANSI_RESET " %s\n", line_num, line);
            matches++;
        }
    }

    if (matches == 0) {
        kprintf("No matches found for '%s' in %s\n", pattern, argv[2]);
    } else {
        kprintf("\n" ANSI_GREEN "%d matches found" ANSI_RESET "\n", matches);
    }
    kprintf("\n");

    vfs_close(fd);
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
