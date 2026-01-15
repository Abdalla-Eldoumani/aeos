/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/editor.h
 * Description: Vim-like text editor interface
 * ============================================================================ */

#ifndef AEOS_EDITOR_H
#define AEOS_EDITOR_H

#include <aeos/types.h>

/* Terminal dimensions */
#define EDITOR_TERM_ROWS    24
#define EDITOR_TERM_COLS    80

/* Editor limits */
#define EDITOR_MAX_FILENAME 256
#define EDITOR_MAX_LINES    1024
#define EDITOR_MAX_LINE_LEN 256
#define EDITOR_MAX_YANK     4096
#define EDITOR_MAX_SEARCH   64
#define EDITOR_MAX_EX_CMD   64

/* Special key codes (returned by editor_read_key) */
#define KEY_ESCAPE      27
#define KEY_ENTER       '\r'
#define KEY_BACKSPACE   127
#define KEY_TAB         '\t'

/* Extended key codes (escape sequences) */
#define KEY_UP          1000
#define KEY_DOWN        1001
#define KEY_RIGHT       1002
#define KEY_LEFT        1003
#define KEY_HOME        1004
#define KEY_END         1005
#define KEY_PAGE_UP     1006
#define KEY_PAGE_DOWN   1007
#define KEY_DELETE      1008
#define KEY_INSERT      1009

/* Editor modes */
typedef enum {
    MODE_COMMAND,       /* Normal mode - navigation and commands */
    MODE_INSERT,        /* Insert mode - typing text */
    MODE_VISUAL,        /* Visual mode - text selection */
    MODE_EX             /* Ex mode - command line (:w, :q, etc.) */
} editor_mode_t;

/* Line structure - each line is a dynamically allocated string */
typedef struct {
    char *chars;        /* Line content */
    size_t len;         /* Current length (excluding null terminator) */
    size_t capacity;    /* Allocated capacity */
} editor_line_t;

/* Selection range for visual mode */
typedef struct {
    int start_row;      /* Starting row */
    int start_col;      /* Starting column */
    int end_row;        /* Ending row */
    int end_col;        /* Ending column */
} editor_selection_t;

/* Editor state */
typedef struct {
    /* File info */
    char filename[EDITOR_MAX_FILENAME];
    bool modified;
    bool new_file;

    /* Buffer - array of lines */
    editor_line_t *lines;
    int num_lines;
    int lines_capacity;

    /* Cursor position */
    int cursor_row;     /* 0-based row in file */
    int cursor_col;     /* 0-based column in line */

    /* Viewport - which part of file is visible */
    int scroll_row;     /* First visible row */
    int scroll_col;     /* First visible column (horizontal scroll) */

    /* Mode */
    editor_mode_t mode;

    /* Visual mode selection */
    editor_selection_t selection;

    /* Yank buffer (clipboard) */
    char yank_buffer[EDITOR_MAX_YANK];
    size_t yank_len;
    bool yank_is_line;  /* True if yanked whole lines */

    /* Search */
    char search_pattern[EDITOR_MAX_SEARCH];
    int search_row;     /* Last search match row */
    int search_col;     /* Last search match col */
    bool search_active;

    /* Ex command line */
    char ex_command[EDITOR_MAX_EX_CMD];
    int ex_len;

    /* Status message */
    char status_msg[80];

    /* Control flags */
    bool should_quit;
    bool redraw_needed;
} editor_t;

/**
 * Run the editor on a file
 * Main entry point - creates editor, runs main loop, cleans up
 *
 * @param filename File to edit (created if doesn't exist)
 */
void editor_run(const char *filename);

/**
 * Initialize editor state
 *
 * @param ed Editor state to initialize
 * @param filename File to edit
 * @return 0 on success, -1 on failure
 */
int editor_init(editor_t *ed, const char *filename);

/**
 * Clean up editor state
 *
 * @param ed Editor state to clean up
 */
void editor_cleanup(editor_t *ed);

/**
 * Read a key from input
 * Handles escape sequences for special keys
 *
 * @return Key code (ASCII or KEY_* constant)
 */
int editor_read_key(void);

/**
 * Process a key in current mode
 *
 * @param ed Editor state
 * @param key Key code
 */
void editor_process_key(editor_t *ed, int key);

/**
 * Refresh the screen
 * Redraws the editor display
 *
 * @param ed Editor state
 */
void editor_refresh_screen(editor_t *ed);

/**
 * Open a file into the editor buffer
 *
 * @param ed Editor state
 * @param filename File to open
 * @return 0 on success, -1 on failure
 */
int editor_open(editor_t *ed, const char *filename);

/**
 * Save editor buffer to file
 *
 * @param ed Editor state
 * @return 0 on success, -1 on failure
 */
int editor_save(editor_t *ed);

/**
 * Set status message
 *
 * @param ed Editor state
 * @param fmt Printf-style format string
 */
void editor_set_status(editor_t *ed, const char *fmt, ...);

#endif /* AEOS_EDITOR_H */
