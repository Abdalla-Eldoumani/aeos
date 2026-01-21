/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/gui.h
 * Description: GUI system integration
 * ============================================================================ */

#ifndef AEOS_GUI_H
#define AEOS_GUI_H

#include <aeos/types.h>

/**
 * Initialize GUI subsystem
 * Sets up event system, window manager, and desktop
 * @return 0 on success, -1 on error
 */
int gui_init(void);

/**
 * Run the GUI
 * This function does not return until GUI exits
 */
void gui_run(void);

/**
 * Request GUI to exit
 */
void gui_exit(void);

/**
 * Check if GUI is running
 */
bool gui_is_running(void);

/**
 * Launch Terminal app
 */
void gui_launch_terminal(void);

/**
 * Launch File Manager app
 */
void gui_launch_filemanager(void);

/**
 * Launch Settings app
 */
void gui_launch_settings(void);

/**
 * Launch About dialog
 */
void gui_launch_about(void);

#endif /* AEOS_GUI_H */
