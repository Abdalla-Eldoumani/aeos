# Section 07: Interactive Shell

## Overview

This section implements an interactive command-line shell for AEOS with line editing, command parsing, and 17 built-in commands. The shell provides the primary user interface for interacting with the operating system.

## Components

### Shell Core (shell.c)
- **Location**: `src/kernel/shell.c`
- **Purpose**: Command-line interface
- **Features**:
  - Line editing with backspace support
  - Command parsing (argc/argv style)
  - Built-in command execution
  - Current working directory tracking
  - Relative/absolute path resolution

## Implemented Commands

| Command | Description | Status |
|---------|-------------|--------|
| help | Show available commands | Working |
| clear | Clear screen (ANSI escape codes) | Working |
| echo | Print arguments to console | Working |
| ps | List process information | Working |
| meminfo | Display memory statistics (PMM + heap) | Working |
| ls | List directory contents | Working |
| cat | Display file contents | Working |
| touch | Create empty file | Working |
| mkdir | Create directory | Working |
| cd | Change working directory | Working |
| pwd | Print working directory | Working |
| rm | Remove file or directory (-rf flags) | Working |
| cp | Copy file | Working |
| mv | Move/rename file | Working |
| save | Save filesystem to disk (stub) | Returns error |
| uname | Show system information | Working |
| exit | Exit shell and halt system | Working |

**Total**: 17 commands

## Shell Features

### Line Editing
- **Backspace**: Delete previous character
- **Printable characters**: Add to input buffer
- **Enter**: Execute command
- **Max line length**: 256 characters (SHELL_MAX_LINE)

### Command Parsing
- **Whitespace separation**: Space and tab delimiters
- **Max arguments**: 16 (SHELL_MAX_ARGS)
- **No quoting**: Spaces in arguments not supported

### Path Resolution
- **Absolute paths**: Start with `/`
- **Relative paths**: Resolved against current working directory
- **Special paths**: `.` (current) and `..` (parent)

### Current Working Directory
- **Default**: `/` (root)
- **Tracking**: Static global variable
- **Path normalization**: Removes trailing slashes except for root

## Shell Loop

```
1. Print prompt "AEOS> "
2. Read line from UART (with backspace support)
3. Parse line into argc/argv
4. Look up command in builtin table
5. Execute command function
6. Repeat
```

## Command Descriptions

### help
Shows list of available commands with descriptions.

### clear
Sends ANSI escape sequence `\033[2J\033[H` to clear screen and move cursor to home.

### echo
Prints all arguments separated by spaces, followed by newline.

**Example**: `echo Hello World` => `Hello World`

### ps
Displays process information from scheduler:
- Total processes created
- Currently running processes
- Total context switches

### meminfo
Shows memory statistics:
- **PMM**: Total/used/free pages
- **Heap**: Total/used/free bytes, allocation counts

### ls [path]
Lists contents of directory (default: current directory).

**Output**:
- `[DIR]  name/` for directories
- `[FILE] name (size bytes)` for files

### cat \<file\>
Reads and displays file contents.

**Implementation**: Reads file in 256-byte chunks, prints to console.

### touch \<file\>
Creates empty file.

**Flags**: `O_CREAT | O_WRONLY`

### mkdir \<dir\>
Creates directory.

**Mode**: 0755

### cd [path]
Changes current working directory.

**Special cases**:
- `cd` or `cd /` => go to root
- `cd ..` => go to parent
- `cd .` => stay in current

### pwd
Prints current working directory.

### rm [-rf] \<file|dir\>
Removes file or directory.

**Flags**:
- `-r`: Recursive (allow directory removal)
- `-f`: Force (suppress errors)

**Implementation**: Tries `vfs_unlink()` first (file), then `vfs_rmdir()` (directory).

### cp \<source\> \<dest\>
Copies file from source to destination.

**Implementation**: Reads source in 512-byte chunks, writes to destination.

### mv \<source\> \<dest\>
Moves/renames file.

**Implementation**: Internally calls `cp` then `rm`.

### save
Attempts to save filesystem to persistent storage.

**Status**: Returns error (persistence not working).

### uname
Shows system information:
- OS name and version
- Architecture (AArch64)
- Platform (QEMU virt)

### exit
Prints goodbye message, then halts system in WFI loop.

## API Reference

### Shell Initialization

```c
/* Initialize shell subsystem */
void shell_init(void);

/* Main shell loop (never returns) */
void shell_run(void);
```

### Line Input

```c
/* Read line with backspace support */
int shell_readline(char *buf, int len);
```

### Command Processing

```c
/* Parse command line into argc/argv */
int shell_parse(char *line, int *argc, char **argv);

/* Execute built-in command */
int shell_execute(int argc, char **argv);
```

## Path Resolution

### resolve_path()

```c
static const char *resolve_path(const char *path)
{
    static char resolved[512];

    /* Absolute path */
    if (path[0] == '/') {
        return path;
    }

    /* Relative path - prepend cwd */
    if (strcmp(cwd, "/") == 0) {
        sprintf(resolved, "/%s", path);
    } else {
        sprintf(resolved, "%s/%s", cwd, path);
    }

    return resolved;
}
```

**Static Buffer**: Returns pointer to static buffer. Not thread-safe.

**Example**:
- cwd = `/home`, path = `file.txt` => `/home/file.txt`
- cwd = `/`, path = `etc` => `/etc`

## Usage Examples

### File Management

```
AEOS> touch test.txt
Created file: test.txt

AEOS> ls
[FILE] test.txt (0 bytes)

AEOS> rm test.txt
Removed: test.txt
```

### Directory Navigation

```
AEOS> mkdir docs
Created directory: docs

AEOS> cd docs
AEOS> pwd
/docs

AEOS> cd ..
AEOS> pwd
/
```

### File I/O

```
AEOS> echo Hello > file.txt       # Shell doesn't support redirection
AEOS> touch file.txt              # Create file manually instead
AEOS> cat file.txt                # Will be empty
```

**Note**: Shell doesn't support I/O redirection (`>`, `<`, `|`).

## Implementation Details

### Command Table

```c
static const shell_cmd_t builtin_commands[] = {
    {"help",    cmd_help,    "Show this help message"},
    {"clear",   cmd_clear,   "Clear the screen"},
    /* ... */
    {NULL,      NULL,        NULL}  /* Terminator */
};
```

**Structure**:
```c
typedef struct {
    const char *name;
    int (*func)(int argc, char **argv);
    const char *description;
} shell_cmd_t;
```

**Lookup**: Linear search through table.

### Current Working Directory

```c
static char cwd[256] = "/";
```

**Global State**: Single static variable. Not per-process.

**Normalization**: Trailing slashes removed except for root.

### Line Editing

```c
int shell_readline(char *buf, int len)
{
    int pos = 0;
    char c;

    while (1) {
        c = uart_getc();

        if (c == '\r' || c == '\n') {
            uart_putc('\n');
            buf[pos] = '\0';
            return pos;
        } else if (c == '\b' || c == 0x7F) {
            /* Backspace */
            if (pos > 0) {
                pos--;
                uart_putc('\b');  /* Move cursor back */
                uart_putc(' ');   /* Overwrite with space */
                uart_putc('\b');  /* Move cursor back again */
            }
        } else if (c >= 32 && c < 127) {
            /* Printable character */
            if (pos < len - 1) {
                buf[pos++] = c;
                uart_putc(c);  /* Echo */
            }
        }
    }
}
```

**Backspace Sequence**: `\b \b` (back, space, back) erases character on terminal.

**Character Filter**: Only accepts printable ASCII (32-126).

## Known Issues

### No Command History
Up/down arrow keys not supported. Can't recall previous commands.

### No Tab Completion
Tab key treated as regular character, not filename completion.

### No I/O Redirection
`>`, `<`, `|`, `>>` not implemented.

### No Quoting
Can't pass arguments with spaces: `echo "Hello World"` doesn't work.

### Global CWD
Current working directory is global, not per-process. All processes share same CWD.

### Static Path Buffers
`resolve_path()` uses static buffer. Not re-entrant.

### No Wildcards
`ls *.txt` doesn't expand wildcard.

### save Command Non-Functional
Filesystem persistence attempted but not working with QEMU.

## Testing

### Command Execution

```
AEOS> help
Available commands:
  help       - Show this help message
  ...
```

### Path Resolution

```
AEOS> pwd
/

AEOS> mkdir test
Created directory: test

AEOS> cd test
AEOS> pwd
/test

AEOS> touch ../file.txt
Created file: file.txt

AEOS> cd ..
AEOS> ls
[DIR]  test/
[FILE] file.txt (0 bytes)
```

### File Operations

```
AEOS> touch src/main.c         # Error - src doesn't exist
AEOS> mkdir src
AEOS> cd src
AEOS> touch main.c
AEOS> ls
[FILE] main.c (0 bytes)
```

### Error Handling

```
AEOS> cat nonexistent.txt
cat: nonexistent.txt: No such file or directory

AEOS> cd nosuchdir
cd: nosuchdir: No such file or directory
```

## Future Enhancements

- Command history (up/down arrows)
- Tab completion
- I/O redirection and pipes
- Environment variables
- Command aliases
- Scripting support
- Job control (background processes)
- Signal handling (Ctrl+C)