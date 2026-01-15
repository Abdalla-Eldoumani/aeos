# Section 07: Interactive Shell

## Overview

This section implements an interactive command-line shell and a vim-like text editor for AEOS. The shell provides 24 built-in commands with colorized output, command history, and filesystem operations. The text editor supports modal editing for creating and modifying files.

## Components

### Shell Core (shell.c)
- **Location**: `src/kernel/shell.c`
- **Purpose**: Command-line interface
- **Features**:
  - 24 built-in commands
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

### No Tab Completion
Tab key is not processed for filename completion.

### No I/O Redirection
Operators like `>`, `<`, `|`, `>>` are not implemented.

### Global Working Directory
Current working directory is global, not per-process.

### Static Path Buffers
`resolve_path()` uses static buffer and is not re-entrant.
