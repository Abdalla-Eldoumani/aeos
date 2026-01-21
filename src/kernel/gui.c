/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/kernel/gui.c
 * Description: GUI system integration
 * ============================================================================ */

#include <aeos/gui.h>
#include <aeos/event.h>
#include <aeos/wm.h>
#include <aeos/desktop.h>
#include <aeos/virtio_input.h>
#include <aeos/virtio_gpu.h>
#include <aeos/framebuffer.h>
#include <aeos/apps/terminal.h>
#include <aeos/apps/filemanager.h>
#include <aeos/apps/settings.h>
#include <aeos/apps/about.h>
#include <aeos/kprintf.h>

/* GUI state */
static bool gui_running = false;

/* Desktop icon launchers */
static void launch_terminal_icon(void)
{
    gui_launch_terminal();
}

static void launch_filemanager_icon(void)
{
    gui_launch_filemanager();
}

static void launch_settings_icon(void)
{
    gui_launch_settings();
}

static void launch_about_icon(void)
{
    gui_launch_about();
}

/**
 * Initialize GUI subsystem
 */
int gui_init(void)
{
    klog_info("Initializing GUI subsystem...");

    /* Initialize event system */
    event_init();

    /* Initialize VirtIO input devices */
    if (virtio_input_init() == 0) {
        klog_info("VirtIO input devices initialized");
    } else {
        klog_warn("VirtIO input devices not available, using UART fallback");
    }

    /* Initialize window manager */
    wm_init();

    /* Initialize desktop */
    desktop_init();

    /* Set desktop as the background paint callback */
    wm_set_desktop_paint(desktop_paint);

    /* Add desktop icons */
    desktop_add_icon("Terminal", 0xFF00AA00, launch_terminal_icon);
    desktop_add_icon("Files", 0xFFDDAA00, launch_filemanager_icon);
    desktop_add_icon("Settings", 0xFF6666AA, launch_settings_icon);
    desktop_add_icon("About", 0xFF0088CC, launch_about_icon);

    klog_info("GUI subsystem initialized");

    return 0;
}

/**
 * Run the GUI
 */
void gui_run(void)
{
    klog_info("Starting GUI...");

    gui_running = true;

    /* Run window manager main loop */
    wm_run();

    gui_running = false;

    klog_info("GUI exited");
}

/**
 * Request GUI to exit
 */
void gui_exit(void)
{
    wm_request_exit();
}

/**
 * Check if GUI is running
 */
bool gui_is_running(void)
{
    return gui_running;
}

/**
 * Launch Terminal app
 */
void gui_launch_terminal(void)
{
    terminal_t *term = terminal_create();
    if (!term) {
        klog_error("Failed to launch Terminal");
    }
}

/**
 * Launch File Manager app
 */
void gui_launch_filemanager(void)
{
    filemanager_t *fm = filemanager_create();
    if (!fm) {
        klog_error("Failed to launch File Manager");
    }
}

/**
 * Launch Settings app
 */
void gui_launch_settings(void)
{
    settings_t *settings = settings_create();
    if (!settings) {
        klog_error("Failed to launch Settings");
    }
}

/**
 * Launch About dialog
 */
void gui_launch_about(void)
{
    about_t *about = about_create();
    if (!about) {
        klog_error("Failed to launch About");
    }
}

/* ============================================================================
 * End of gui.c
 * ============================================================================ */
