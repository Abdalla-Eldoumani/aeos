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

#endif /* AEOS_SHELL_H */

/* ============================================================================
 * End of shell.h
 * ============================================================================ */
