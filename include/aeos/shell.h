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

#endif /* AEOS_SHELL_H */

/* ============================================================================
 * End of shell.h
 * ============================================================================ */
