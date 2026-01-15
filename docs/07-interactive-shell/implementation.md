# Interactive Shell - Implementation Details

## Shell Main Loop

### shell_run()

```c
void shell_run(void)
{
    char line[SHELL_MAX_LINE];      /* 256 bytes */
    char *argv[SHELL_MAX_ARGS];     /* 16 pointers */
    int argc;

    print_banner();

    while (1) {
        kprintf(ANSI_GREEN "AEOS" ANSI_RESET "> ");
        shell_readline(line, SHELL_MAX_LINE);
        shell_parse(line, &argc, argv);
        if (argc > 0) {
            shell_execute(argc, argv);
        }
    }
}
```

The shell runs in an infinite loop, reading commands and executing them.

## Line Input

### shell_readline()

```c
int shell_readline(char *buf, int maxlen)
{
    int pos = 0;
    char c;

    while (1) {
        c = uart_getc();  /* Blocking read */

        if (c == '\r' || c == '\n') {
            uart_putc('\n');
            buf[pos] = '\0';
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
    }
}
```

Characters are echoed as typed. Both backspace (0x08) and DEL (0x7F) are handled.

## Command History

```c
#define HISTORY_SIZE 32
#define HISTORY_LINE_LEN 256

static char history[HISTORY_SIZE][HISTORY_LINE_LEN];
static int history_count = 0;
static int history_start = 0;

static void history_add(const char *cmd)
{
    /* Don't add empty commands */
    if (cmd[0] == '\0') return;

    /* Don't add duplicates of last command */
    if (history_count > 0) {
        int last_idx = (history_start + history_count - 1) % HISTORY_SIZE;
        if (strcmp(history[last_idx], cmd) == 0) return;
    }

    /* Add to circular buffer */
    int idx = (history_start + history_count) % HISTORY_SIZE;
    strncpy(history[idx], cmd, HISTORY_LINE_LEN - 1);
    history[idx][HISTORY_LINE_LEN - 1] = '\0';

    if (history_count < HISTORY_SIZE) {
        history_count++;
    } else {
        history_start = (history_start + 1) % HISTORY_SIZE;
    }
}
```

History is stored in a circular buffer. The `history` command displays all stored commands.

## Command Parsing

### shell_parse()

```c
int shell_parse(char *line, int *argc, char **argv)
{
    int i = 0;
    char *p = line;

    *argc = 0;

    /* Skip leading whitespace */
    while (*p == ' ' || *p == '\t') p++;

    /* Parse arguments */
    while (*p != '\0' && i < SHELL_MAX_ARGS) {
        argv[i++] = p;

        /* Find end of argument */
        while (*p != '\0' && *p != ' ' && *p != '\t') p++;

        /* Null-terminate argument */
        if (*p != '\0') {
            *p++ = '\0';
            while (*p == ' ' || *p == '\t') p++;
        }
    }

    *argc = i;
    return 0;
}
```

The parser modifies the input buffer in place, inserting null terminators between arguments.

## Command Execution

### shell_execute()

```c
int shell_execute(int argc, char **argv)
{
    if (argc == 0) return 0;

    /* Look for built-in command */
    for (int i = 0; builtin_commands[i].name != NULL; i++) {
        if (strcmp(argv[0], builtin_commands[i].name) == 0) {
            return builtin_commands[i].func(argc, argv);
        }
    }

    /* Command not found */
    kprintf(ANSI_RED "%s: command not found" ANSI_RESET "\n", argv[0]);
    kprintf("Type 'help' for available commands\n");
    return -1;
}
```

Commands are looked up in a table of function pointers.

## Path Resolution

### resolve_path()

```c
static const char *resolve_path(const char *path)
{
    static char resolved[512];

    if (path == NULL || path[0] == '\0') return cwd;
    if (path[0] == '/') return path;

    /* Relative path - combine with cwd */
    if (strcmp(cwd, "/") == 0) {
        snprintf(resolved, sizeof(resolved), "/%s", path);
    } else {
        snprintf(resolved, sizeof(resolved), "%s/%s", cwd, path);
    }

    return resolved;
}
```

Converts relative paths to absolute paths using the current working directory.

## New Commands

### cmd_write()

```c
static int cmd_write(int argc, char **argv)
{
    if (argc < 3) {
        kprintf("Usage: write <filename> <text...>\n");
        return -1;
    }

    const char *path = resolve_path(argv[1]);
    int fd = vfs_open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        kprintf(ANSI_RED "write: cannot open '%s'" ANSI_RESET "\n", argv[1]);
        return -1;
    }

    /* Write all arguments as text */
    for (int i = 2; i < argc; i++) {
        vfs_write(fd, argv[i], strlen(argv[i]));
        if (i < argc - 1) vfs_write(fd, " ", 1);
    }
    vfs_write(fd, "\n", 1);

    vfs_close(fd);
    return 0;
}
```

### cmd_grep()

```c
static int cmd_grep(int argc, char **argv)
{
    if (argc < 3) {
        kprintf("Usage: grep <pattern> <filename>\n");
        return -1;
    }

    const char *pattern = argv[1];

    /* Strip leading/trailing quotes from pattern */
    int plen = strlen(pattern);
    if (plen >= 2) {
        char first = pattern[0];
        char last = pattern[plen - 1];
        if ((first == '"' && last == '"') ||
            (first == '\'' && last == '\'')) {
            static char unquoted[256];
            strncpy(unquoted, pattern + 1, plen - 2);
            unquoted[plen - 2] = '\0';
            pattern = unquoted;
        }
    }

    /* Open file and search line by line */
    /* ... */
}
```

The grep command strips quotes from patterns so `grep "test" file.txt` works correctly.

### cmd_hexdump()

```c
static int cmd_hexdump(int argc, char **argv)
{
    /* Read file in chunks */
    /* Display: offset, hex bytes, ASCII representation */
    /* Example output:
       00000000  48 65 6c 6c 6f 20 57 6f  72 6c 64 0a              |Hello World.|
    */
}
```

### cmd_time()

```c
static int cmd_time(int argc, char **argv)
{
    if (argc < 2) {
        kprintf("Usage: time <command> [args...]\n");
        return -1;
    }

    uint64_t start = timer_get_ticks();

    /* Execute the command */
    shell_execute(argc - 1, &argv[1]);

    uint64_t end = timer_get_ticks();
    uint64_t elapsed_ms = ((end - start) * 1000) / TIMER_FREQ_HZ;

    kprintf("\nTime: %u ms\n", (uint32_t)elapsed_ms);
    return 0;
}
```

## Colorized Output

```c
#define ANSI_RESET     "\033[0m"
#define ANSI_RED       "\033[31m"
#define ANSI_GREEN     "\033[32m"
#define ANSI_YELLOW    "\033[33m"
#define ANSI_BLUE      "\033[34m"
#define ANSI_CYAN      "\033[36m"
```

Colors are applied using ANSI escape codes:
- Green: Prompt, success messages
- Red: Error messages
- Blue: Directory names in `ls`
- Cyan: Headers, line numbers in `grep`

## Text Editor Implementation

### Editor State

```c
typedef struct {
    char **lines;           /* Array of line pointers */
    int num_lines;          /* Number of lines */
    int cursor_row;         /* Current row (0-based) */
    int cursor_col;         /* Current column (0-based) */
    int scroll_offset;      /* First visible line */
    editor_mode_t mode;     /* NORMAL, INSERT, or EX */
    char filename[256];     /* Current file */
    bool modified;          /* Has unsaved changes */
    char ex_buffer[64];     /* EX mode command buffer */
    int ex_pos;             /* Position in EX buffer */
} editor_state_t;
```

### Main Editor Loop

```c
void editor_open(const char *filename)
{
    editor_state_t editor;
    editor_init(&editor, filename);
    editor_load_file(&editor);

    while (1) {
        editor_render(&editor);
        int key = editor_read_key();

        switch (editor.mode) {
            case MODE_NORMAL:
                if (!editor_handle_normal(&editor, key)) return;
                break;
            case MODE_INSERT:
                editor_handle_insert(&editor, key);
                break;
            case MODE_EX:
                if (!editor_handle_ex(&editor, key)) return;
                break;
        }
    }
}
```

### Key Handling in Normal Mode

```c
static bool editor_handle_normal(editor_state_t *e, int key)
{
    switch (key) {
        case 'h': case KEY_LEFT:  editor_move_left(e);  break;
        case 'j': case KEY_DOWN:  editor_move_down(e);  break;
        case 'k': case KEY_UP:    editor_move_up(e);    break;
        case 'l': case KEY_RIGHT: editor_move_right(e); break;
        case 'i': e->mode = MODE_INSERT; break;
        case 'x': editor_delete_char(e); break;
        case ':': e->mode = MODE_EX; e->ex_pos = 0; break;
        /* ... */
    }
    return true;
}
```

### Screen Rendering

```c
static void editor_render(editor_state_t *e)
{
    /* Clear screen */
    kprintf("\033[2J\033[H");

    /* Draw visible lines with line numbers */
    for (int i = 0; i < EDITOR_ROWS; i++) {
        int line_num = e->scroll_offset + i;
        if (line_num < e->num_lines) {
            kprintf("%4d | %s\n", line_num + 1, e->lines[line_num]);
        } else {
            kprintf("   ~ |\n");
        }
    }

    /* Draw status bar */
    kprintf("-- %s -- %s %s\n",
            mode_names[e->mode],
            e->filename,
            e->modified ? "[+]" : "");

    /* Position cursor */
    int screen_row = e->cursor_row - e->scroll_offset + 1;
    int screen_col = e->cursor_col + 7;  /* Account for line number width */
    kprintf("\033[%d;%dH", screen_row, screen_col);
}
```

## Debugging

### Trace Command Execution

```c
/* Add to shell_execute() */
kprintf("[DEBUG] Executing: %s (argc=%d)\n", argv[0], argc);
for (int i = 0; i < argc; i++) {
    kprintf("  argv[%d] = '%s'\n", i, argv[i]);
}
```

### Check History State

```c
/* View internal history state */
kprintf("History: count=%d start=%d\n", history_count, history_start);
```

## Performance Notes

- Command lookup is O(n) linear search through command table
- Path resolution uses static buffer (not thread-safe)
- Editor stores entire file in memory (limited by heap size)
- History uses fixed-size circular buffer (no dynamic allocation)
