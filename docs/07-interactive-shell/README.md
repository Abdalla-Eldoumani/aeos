# Section 07: Interactive Shell

## Overview

This section implements an interactive command-line shell and a vim-like text editor for AEOS. The shell provides 30 built-in commands with colorized output, command history, and filesystem operations. The text editor supports modal editing for creating and modifying files.

## Components

### Shell Core (shell.c)
- **Location**: `src/kernel/shell.c`
- **Purpose**: Command-line interface
- **Features**:
  - 30 built-in commands
  - Colorized output (ANSI escape codes)
  - Command history storage
  - Line editing with backspace
  - Current working directory tracking
  - Relative/absolute path resolution

### Text Editor (editor.c)
- **Location**: `src/kernel/editor.c`
- **Purpose**: Vim-like file editor
- **Features**:
  - Modal editing (NORMAL, INSERT, EX modes)
  - Line-based editing with scrolling
  - File save/load via VFS
  - Line numbers display
  - Status bar with mode indicator

## Shell Commands

| Command | Description |
|---------|-------------|
| help | Show available commands |
| clear | Clear screen (ANSI escape codes) |
| echo | Print arguments to console |
| ls | List directory contents |
| cat | Display file contents |
| touch | Create empty file |
| mkdir | Create directory |
| rm | Remove file or directory (-rf flags) |
| cp | Copy file |
| mv | Move/rename file |
| cd | Change working directory |
| pwd | Print working directory |
| write | Write text to file |
| hexdump | Hex dump of file contents |
| grep | Search for pattern in file |
| edit / vi | Open vim-like text editor |
| ps | List process information |
| meminfo | Display memory statistics |
| uptime | Show system uptime |
| irqinfo | Show interrupt statistics |
| history | Show command history |
| time | Time command execution |
| uname | Show system information |
| save | Save filesystem to host |
| exec | Load and run a static ELF64 at EL0 |
| kill | Kill a registered process by PID |
| ping | Ping an IPv4 host with a bounded ICMP echo |
| startx | Start the graphical desktop |
| exit | Exit shell and halt system |

## Shell Features

### Line Editing
- Backspace: Delete previous character
- Printable characters: Add to input buffer
- Enter: Execute command
- Max line length: 256 characters

### Command Parsing
- Whitespace separation (space and tab)
- Max arguments: 16
- Quote stripping for grep patterns
- A command line is split on the `|` pipe character into stages before parsing each stage

### Pipes
The shell splits a command line on `|` and runs the stages serially. Built-ins always print through `kprintf`, so a pipe stage installs a `kprintf` output hook that appends each character into a small fixed ring (256 bytes); the next stage reads that captured output back through `shell_pipe_readline` instead of opening a file. There are no real processes or kernel pipes behind this; the stages run one after another, up to a small fixed number of stages. A built-in opts into pipe input by reading `shell_pipe_readline` when its argv has no filename, as `grep` does.

### Loaded Programs and Networking
- `exec <path>` loads a static ELF64 off the VFS and runs it at EL0 (see Section 04). The embedded `/hello` is the in-tree example; a negative load is reported and the shell continues.
- `kill <pid>` looks the pid up in the process registry and arms its kill flag, honored at the program's next `svc`. It refuses a non-killable pid (idle and the kernel threads), so `kill 1` reports failure rather than a misleading success.
- `ping <ip>` sends one ICMP echo. The address is a dotted-quad (four 0-255 octets, parsed the way `kill` parses a pid; no DNS); with no argument it defaults to the slirp gateway `10.0.2.2`. The round trip is bounded inside `net_ping`, so `ping` never hangs the prompt; it prints the reply, a timeout, or "no network device". Only `10.0.2.2` is guaranteed to reply in this build.

### Path Resolution
- Absolute paths start with `/`
- Relative paths resolved against current working directory
- Special paths: `.` (current) and `..` (parent)

### Colorized Output
- Directories shown in blue
- Errors shown in red
- Prompts and headers in green/cyan

### Command History
- Stores last 32 commands
- View with `history` command
- Duplicate commands not stored consecutively
- Persists across reboots. The `save` command writes the ring to the host file
  `aeos_hist.img` over semihosting (a magic-versioned blob, oldest-first), right
  after it saves the filesystem. `shell_init` loads it at boot. The save is a
  deliberate `save` action only - there is no per-command write, which would
  pause the kernel on every Enter. A missing, foreign, or malformed image is
  validated (magic, version, count, line length) and rejected to an empty
  history, so a bad file never hangs or faults the boot path.

## Text Editor

### Modes

**NORMAL Mode** (default):
- `h/j/k/l` or arrows: Move cursor
- `0`: Start of line
- `$`: End of line
- `gg`: First line
- `G`: Last line
- `i`: Enter INSERT mode at cursor
- `a`: Enter INSERT mode after cursor
- `o`: Open line below and enter INSERT
- `x`: Delete character at cursor
- `dd`: Delete current line
- `:`: Enter EX mode

**INSERT Mode**:
- Type to insert characters
- Backspace: Delete previous character
- Enter: Insert newline
- Escape: Return to NORMAL mode

**EX Mode** (command line):
- `:w` - Save file
- `:q` - Quit (warns if modified)
- `:q!` - Force quit without saving
- `:wq` - Save and quit

### Display
- Line numbers in left margin
- Mode indicator in status bar
- Filename and modification status shown

### Buffer Growth and Memory Safety
The editor grows two buffers, and both growth sites are overflow-guarded (SEC-02):

- `editor_add_line` grows the line array (through `editor_grow_lines`). It rejects a growth whose `new_cap * sizeof(editor_line_t)` byte size would overflow.
- `line_grow` grows a single line's character buffer. It rejects a `*2` doubling that would wrap.

Both use the `((size_t)-1) / elem` idiom from `kcalloc` (`SIZE_MAX` is absent from this tree). On an overflow or a `kmalloc` failure the existing buffer is left intact and the function returns -1; it never hands back an undersized block that the following copy would overrun. `editor_add_line` surfaces the controlled out-of-memory path with `notify_error("Out of memory")` plus a status line.

`editor_set_status` is copy-only: it `strncpy`s the format string and ignores any varargs, so callers pass a plain literal. A `%` specifier left in a status string prints verbatim.

## API Reference

### Shell Functions

```c
/* Initialize shell subsystem */
void shell_init(void);

/* Main shell loop (never returns) */
void shell_run(void);

/* Read line with backspace support */
int shell_readline(char *buf, int len);
```

### Editor Functions

```c
/* Open editor with file */
void editor_open(const char *filename);
```

## Usage Examples

### File Management

```
AEOS> touch test.txt
Created file: test.txt

AEOS> write test.txt Hello World
Wrote 11 bytes to test.txt

AEOS> cat test.txt
Hello World

AEOS> grep World test.txt
1: Hello World
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

### Text Editor

```
AEOS> edit notes.txt
[Opens editor in NORMAL mode]
[Press 'i' to enter INSERT mode]
[Type your content]
[Press Escape to return to NORMAL mode]
[Type ':wq' to save and quit]
```

### Filesystem Persistence

```
AEOS> touch important.txt
AEOS> write important.txt This will persist
AEOS> save
Filesystem saved successfully!
[Exit and restart QEMU]
AEOS> cat important.txt
This will persist
```

## Known Limitations

### No Arrow Key Navigation
Arrow keys generate escape sequences that cause SError exceptions. The escape sequence parser is disabled, so arrow keys appear as literal characters. Command history is stored but cannot be navigated with up/down arrows.

### No Filename Completion
The input loop reads UART directly and does not process the Tab key, so there is no filename completion.

### Limited Redirection
The pipe operator `|` is supported (see Pipes below). File redirection operators `>`, `<`, and `>>` are not implemented.

### Global Working Directory
Current working directory is global, not per-process.

### Static Path Buffers
`resolve_path()` uses static buffer and is not re-entrant.
