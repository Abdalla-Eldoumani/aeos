/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/shell.h
 * Description: Interactive shell interface
 * ============================================================================ */

#ifndef AEOS_SHELL_H
#define AEOS_SHELL_H

#include <aeos/types.h>

/* Maximum command line length */
#define SHELL_MAX_LINE 256

/* Maximum number of arguments */
#define SHELL_MAX_ARGS 16

/**
 * Initialize shell subsystem
 */
void shell_init(void);

/**
 * Run interactive shell (main loop)
 * This function does not return
 */
void shell_run(void);

/**
 * Read a line of input with editing support
 * @param buf Buffer to store input
 * @param len Buffer length
 * @return Number of characters read
 */
int shell_readline(char *buf, int len);

/**
 * Parse command line into argc/argv
 * @param line Input line
 * @param argc Output: argument count
 * @param argv Output: argument array
 * @return 0 on success, -1 on error
 */
int shell_parse(char *line, int *argc, char **argv);

/**
 * Execute a command
 * @param argc Argument count
 * @param argv Argument array
 * @return 0 on success, -1 on error
 */
int shell_execute(int argc, char **argv);

/**
 * Run a single command line, splitting it on `|` into pipe stages.
 *
 * Each non-final stage's stdout (kprintf) is captured into a small ring
 * buffer that the next stage reads from via shell_pipe_readline. The final
 * stage writes through the kprintf hook that was active on entry, so this
 * is safe to call from both the text-mode shell loop and the GUI terminal
 * (which redirects kprintf to its own cell buffer).
 *
 * The line is mutated in place during tokenization.
 */
void shell_run_line(char *line);

/**
 * Read one line from the active input pipe (set by shell_run_line for
 * non-first pipe stages). Built-ins that want to support being on the
 * read side of a pipe call this when their argv lacks a filename.
 *
 * Returns line length, 0 for an empty line, -1 if no more input.
 */
int shell_pipe_readline(char *buf, int max);

/**
 * Whether a pipe input source is currently active. Built-ins use this to
 * decide whether to drop into pipe-read mode or print a usage message.
 */
bool shell_has_pipe_input(void);

/**
 * Persist the command history ring to the host file aeos_hist.img via the raw
 * semihosting primitives (a magic-versioned blob, not the VFS serializer).
 * Triggered from cmd_save after the FS save so one deliberate `save` persists
 * both. Gated on semihost_available; a write failure is logged and returns -1
 * rather than hanging, so it never turns a successful FS save into a failure.
 *
 * @return 0 on success, -1 if semihosting is unavailable or the write failed
 */
int history_save(void);

/**
 * Load the command history ring from aeos_hist.img at boot (called from
 * shell_init in place of the unconditional clear). Validates the magic,
 * version, and bounds before reading any records and falls back to an empty
 * buffer on a missing, foreign, or malformed image. Never loops unbounded and
 * never faults the boot path.
 */
void history_load(void);

#ifdef TEST_BUILD
/*
 * History buffer test seam (mirrors the editor_test_/syscall_test_/
 * scheduler_test_ seams). The history ring and its counters are file-static in
 * shell.c, so test_history_persist_roundtrip uses these thin wrappers to seed
 * and inspect the buffer without it being private. Compiled out of production
 * (no prod symbol, no -Werror unused warning).
 */
void shell_test_history_reset(void);             /* clear the ring to empty */
void shell_test_history_seed(const char *line);  /* history_add a known line */
int  shell_test_history_count(void);             /* live entry count */
const char *shell_test_history_get(int rel_idx); /* history_get (0 = newest) */
#endif /* TEST_BUILD */

#endif /* AEOS_SHELL_H */

/* ============================================================================
 * End of shell.h
 * ============================================================================ */
