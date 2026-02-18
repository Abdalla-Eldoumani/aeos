/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/apps/filemanager.c
 * Description: File manager application
 * ============================================================================ */

#include <aeos/apps/filemanager.h>
#include <aeos/window.h>
#include <aeos/wm.h>
#include <aeos/framebuffer.h>
#include <aeos/vfs.h>
#include <aeos/heap.h>
#include <aeos/string.h>
#include <aeos/kprintf.h>

/* Colors */
#define FM_BG_COLOR         0xFF1A1A2E
#define FM_PATH_BG          0xFF252540
#define FM_ENTRY_BG         0xFF202035
#define FM_ENTRY_SELECTED   0xFF303055
#define FM_FOLDER_COLOR     0xFF5588FF
#define FM_FILE_COLOR       0xFFCCCCCC
#define FM_BORDER_COLOR     0xFF404060

/* Layout */
#define FM_PATH_HEIGHT      24
#define FM_ENTRY_HEIGHT     20
#define FM_ICON_WIDTH       16
#define FM_PADDING          8

/* Forward declarations */
static void filemanager_paint(window_t *win);
static void filemanager_key(window_t *win, key_event_t *key);
static void filemanager_mouse(window_t *win, mouse_event_t *mouse);
static void filemanager_close(window_t *win);

/**
 * Create file manager
 */
filemanager_t *filemanager_create(void)
{
    filemanager_t *fm;

    fm = (filemanager_t *)kmalloc(sizeof(filemanager_t));
    if (!fm) {
        klog_error("Failed to allocate file manager");
        return NULL;
    }

    memset(fm, 0, sizeof(filemanager_t));

    /* Create window */
    fm->window = window_create("Files", 150, 80, 400, 320,
                                WINDOW_FLAG_VISIBLE);
    if (!fm->window) {
        kfree(fm);
        return NULL;
    }

    /* Set callbacks */
    fm->window->on_paint = filemanager_paint;
    fm->window->on_key = filemanager_key;
    fm->window->on_mouse = filemanager_mouse;
    fm->window->on_close = filemanager_close;
    fm->window->user_data = fm;

    /* Initialize state */
    strcpy(fm->current_path, "/");
    fm->selected_index = -1;
    fm->scroll_offset = 0;
    fm->visible_entries = (fm->window->client_height - FM_PATH_HEIGHT - FM_PADDING) /
                          FM_ENTRY_HEIGHT;

    /* Register and load contents */
    wm_register_window(fm->window);
    filemanager_refresh(fm);

    klog_debug("File manager created");

    return fm;
}

/**
 * Destroy file manager
 */
void filemanager_destroy(filemanager_t *fm)
{
    if (!fm) {
        return;
    }

    kfree(fm);
}

/**
 * Refresh file list
 */
void filemanager_refresh(filemanager_t *fm)
{
    int dir_fd;
    vfs_dirent_t dirent;
    uint32_t index = 0;

    if (!fm) {
        return;
    }

    fm->entry_count = 0;

    /* Add parent directory entry if not root */
    if (strcmp(fm->current_path, "/") != 0) {
        strcpy(fm->entries[index].name, "..");
        fm->entries[index].is_directory = true;
        fm->entries[index].size = 0;
        index++;
    }

    /* Open directory */
    dir_fd = vfs_open(fm->current_path, O_RDONLY, 0);
    if (dir_fd < 0) {
        klog_error("Failed to open %s", fm->current_path);
        fm->entry_count = index;
        return;
    }

    /* Read entries */
    while (vfs_readdir(dir_fd, &dirent) == 0 && index < 64) {
        /* Skip . and .. from VFS (we add our own ..) */
        if (strcmp(dirent.name, ".") == 0 || strcmp(dirent.name, "..") == 0) {
            continue;
        }

        strncpy(fm->entries[index].name, dirent.name, 63);
        fm->entries[index].name[63] = '\0';

        /* Use dirent info directly */
        fm->entries[index].is_directory = (dirent.type == VFS_FILE_DIRECTORY);
        fm->entries[index].size = (uint32_t)dirent.size;

        index++;
    }

    vfs_close(dir_fd);
    fm->entry_count = index;

    window_invalidate(fm->window);
}

/**
 * Navigate to directory
 */
void filemanager_navigate(filemanager_t *fm, const char *path)
{
    int dir_fd;

    if (!fm || !path) {
        return;
    }

    /* Try to open the path */
    dir_fd = vfs_open(path, O_RDONLY, 0);
    if (dir_fd >= 0) {
        vfs_close(dir_fd);
        strncpy(fm->current_path, path, 255);
        fm->current_path[255] = '\0';
        fm->selected_index = -1;
        fm->scroll_offset = 0;
        filemanager_refresh(fm);
    }
}

/**
 * Draw a folder icon
 */
static void draw_folder_icon(window_t *win, int32_t x, int32_t y)
{
    window_fill_rect(win, x, y + 2, 14, 10, FM_FOLDER_COLOR);
    window_fill_rect(win, x, y, 6, 3, FM_FOLDER_COLOR);
}

/**
 * Draw a file icon
 */
static void draw_file_icon(window_t *win, int32_t x, int32_t y)
{
    window_fill_rect(win, x + 2, y, 10, 14, FM_FILE_COLOR);
    window_fill_rect(win, x + 8, y, 4, 4, FM_BG_COLOR);  /* Corner fold */
    window_draw_line(win, x + 8, y, x + 12, y + 4, FM_FILE_COLOR);
}

/**
 * Open a file for viewing
 */
static void filemanager_view_file(filemanager_t *fm, const char *name)
{
    char filepath[256];
    int fd;
    ssize_t bytes_read;

    /* Build full path */
    if (strcmp(fm->current_path, "/") == 0) {
        snprintf(filepath, sizeof(filepath), "/%s", name);
    } else {
        snprintf(filepath, sizeof(filepath), "%s/%s", fm->current_path, name);
    }

    fd = vfs_open(filepath, O_RDONLY, 0);
    if (fd < 0) {
        return;
    }

    bytes_read = vfs_read(fd, fm->view_content, sizeof(fm->view_content) - 1);
    vfs_close(fd);

    if (bytes_read < 0) bytes_read = 0;
    fm->view_content[bytes_read] = '\0';
    fm->view_content_len = (uint32_t)bytes_read;

    strncpy(fm->view_filename, name, sizeof(fm->view_filename) - 1);
    fm->view_filename[sizeof(fm->view_filename) - 1] = '\0';
    fm->viewing_file = true;
    fm->view_scroll = 0;

    window_invalidate(fm->window);
}

/**
 * Draw file viewer content
 */
static void filemanager_paint_viewer(window_t *win, filemanager_t *fm)
{
    int32_t y;
    uint32_t line_num = 0;
    char line_buf[128];
    uint32_t line_pos = 0;
    uint32_t i;

    /* Clear background */
    window_clear(win, FM_BG_COLOR);

    /* Draw header bar */
    window_fill_rect(win, 0, 0, win->client_width, FM_PATH_HEIGHT, FM_PATH_BG);
    window_puts(win, FM_PADDING, 8, fm->view_filename, 0xFFFFFFFF, FM_PATH_BG);
    window_puts(win, win->client_width - 88, 8, "[Bksp]", 0xFF888888, FM_PATH_BG);

    /* Draw separator */
    window_fill_rect(win, 0, FM_PATH_HEIGHT, win->client_width, 1, FM_BORDER_COLOR);

    /* Draw file content line by line */
    y = FM_PATH_HEIGHT + 4;

    for (i = 0; i <= fm->view_content_len; i++) {
        char c = (i < fm->view_content_len) ? fm->view_content[i] : '\n';

        if (c == '\n' || c == '\r' || line_pos >= sizeof(line_buf) - 1) {
            line_buf[line_pos] = '\0';

            if (line_num >= fm->view_scroll) {
                if (y + 10 > (int32_t)win->client_height) break;
                window_puts(win, FM_PADDING, y + 2, line_buf, FM_FILE_COLOR, FM_BG_COLOR);
                y += 12;
            }

            line_num++;
            line_pos = 0;

            /* Skip \r\n pair */
            if (c == '\r' && i + 1 < fm->view_content_len && fm->view_content[i + 1] == '\n') {
                i++;
            }
        } else {
            line_buf[line_pos++] = c;
        }
    }
}

/**
 * Draw file manager content
 */
static void filemanager_paint(window_t *win)
{
    filemanager_t *fm = (filemanager_t *)win->user_data;
    int32_t y;
    uint32_t i;

    if (!fm) {
        return;
    }

    /* File viewer mode */
    if (fm->viewing_file) {
        filemanager_paint_viewer(win, fm);
        return;
    }

    /* Clear background */
    window_clear(win, FM_BG_COLOR);

    /* Draw path bar */
    window_fill_rect(win, 0, 0, win->client_width, FM_PATH_HEIGHT, FM_PATH_BG);
    window_puts(win, FM_PADDING, 8, fm->current_path, 0xFFFFFFFF, FM_PATH_BG);

    /* Draw separator */
    window_fill_rect(win, 0, FM_PATH_HEIGHT, win->client_width, 1, FM_BORDER_COLOR);

    /* Draw file entries */
    y = FM_PATH_HEIGHT + 4;

    for (i = fm->scroll_offset; i < fm->entry_count && y < (int32_t)(win->client_height - FM_ENTRY_HEIGHT); i++) {
        file_entry_t *entry = &fm->entries[i];
        uint32_t bg = (int32_t)i == fm->selected_index ? FM_ENTRY_SELECTED : FM_ENTRY_BG;
        uint32_t text_color = entry->is_directory ? FM_FOLDER_COLOR : FM_FILE_COLOR;

        /* Entry background */
        window_fill_rect(win, FM_PADDING - 2, y, win->client_width - FM_PADDING * 2 + 4,
                         FM_ENTRY_HEIGHT - 2, bg);

        /* Icon */
        if (entry->is_directory) {
            draw_folder_icon(win, FM_PADDING, y + 2);
        } else {
            draw_file_icon(win, FM_PADDING, y + 1);
        }

        /* Name */
        window_puts(win, FM_PADDING + FM_ICON_WIDTH + 4, y + 6, entry->name,
                    text_color, bg);

        /* Size (for files) */
        if (!entry->is_directory && entry->size > 0) {
            char size_str[16];
            if (entry->size >= 1024) {
                snprintf(size_str, sizeof(size_str), "%uKB", entry->size / 1024);
            } else {
                snprintf(size_str, sizeof(size_str), "%uB", entry->size);
            }
            window_puts(win, win->client_width - 60, y + 6, size_str,
                        0xFF888888, bg);
        }

        y += FM_ENTRY_HEIGHT;
    }
}

/**
 * Handle key input
 */
static void filemanager_key(window_t *win, key_event_t *key)
{
    filemanager_t *fm = (filemanager_t *)win->user_data;

    if (!fm) {
        return;
    }

    /* File viewer mode: Backspace exits, Up/Down scrolls */
    if (fm->viewing_file) {
        switch (key->keycode) {
            case KEY_BACKSPACE:
            case KEY_ESCAPE:
                fm->viewing_file = false;
                break;
            case KEY_UP:
                if (fm->view_scroll > 0) fm->view_scroll--;
                break;
            case KEY_DOWN:
                fm->view_scroll++;
                break;
            default:
                break;
        }
        window_invalidate(win);
        return;
    }

    switch (key->keycode) {
        case KEY_UP:
            if (fm->selected_index > 0) {
                fm->selected_index--;
                if ((uint32_t)fm->selected_index < fm->scroll_offset) {
                    fm->scroll_offset = fm->selected_index;
                }
            }
            break;

        case KEY_DOWN:
            if (fm->selected_index < (int32_t)fm->entry_count - 1) {
                fm->selected_index++;
                if ((uint32_t)fm->selected_index >= fm->scroll_offset + fm->visible_entries) {
                    fm->scroll_offset = fm->selected_index - fm->visible_entries + 1;
                }
            }
            break;

        case KEY_ENTER:
            if (fm->selected_index >= 0 && fm->selected_index < (int32_t)fm->entry_count) {
                file_entry_t *entry = &fm->entries[fm->selected_index];
                if (entry->is_directory) {
                    char new_path[256];
                    if (strcmp(entry->name, "..") == 0) {
                        /* Go to parent */
                        strncpy(new_path, fm->current_path, 255);
                        char *last_slash = strrchr(new_path, '/');
                        if (last_slash && last_slash != new_path) {
                            *last_slash = '\0';
                        } else {
                            strcpy(new_path, "/");
                        }
                    } else {
                        if (strcmp(fm->current_path, "/") == 0) {
                            snprintf(new_path, sizeof(new_path), "/%s", entry->name);
                        } else {
                            snprintf(new_path, sizeof(new_path), "%s/%s",
                                     fm->current_path, entry->name);
                        }
                    }
                    filemanager_navigate(fm, new_path);
                } else {
                    /* View file contents */
                    filemanager_view_file(fm, entry->name);
                }
            }
            break;

        case KEY_BACKSPACE:
            /* Go to parent directory */
            if (strcmp(fm->current_path, "/") != 0) {
                char new_path[256];
                strncpy(new_path, fm->current_path, 255);
                char *last_slash = strrchr(new_path, '/');
                if (last_slash && last_slash != new_path) {
                    *last_slash = '\0';
                } else {
                    strcpy(new_path, "/");
                }
                filemanager_navigate(fm, new_path);
            }
            break;

        default:
            break;
    }

    window_invalidate(win);
}

/**
 * Handle mouse input
 */
static void filemanager_mouse(window_t *win, mouse_event_t *mouse)
{
    filemanager_t *fm = (filemanager_t *)win->user_data;
    int32_t clicked_index;

    if (!fm) {
        return;
    }

    /* In file viewer mode, clicking does nothing special */
    if (fm->viewing_file) {
        window_invalidate(win);
        return;
    }

    /* Calculate clicked entry */
    if (mouse->y > FM_PATH_HEIGHT + 4) {
        clicked_index = fm->scroll_offset + (mouse->y - FM_PATH_HEIGHT - 4) / FM_ENTRY_HEIGHT;
        if (clicked_index >= 0 && clicked_index < (int32_t)fm->entry_count) {
            if (fm->selected_index == clicked_index) {
                /* Double click simulation - navigate/view if same entry */
                file_entry_t *entry = &fm->entries[clicked_index];
                if (entry->is_directory) {
                    char new_path[256];
                    if (strcmp(entry->name, "..") == 0) {
                        strncpy(new_path, fm->current_path, 255);
                        char *last_slash = strrchr(new_path, '/');
                        if (last_slash && last_slash != new_path) {
                            *last_slash = '\0';
                        } else {
                            strcpy(new_path, "/");
                        }
                    } else {
                        if (strcmp(fm->current_path, "/") == 0) {
                            snprintf(new_path, sizeof(new_path), "/%s", entry->name);
                        } else {
                            snprintf(new_path, sizeof(new_path), "%s/%s",
                                     fm->current_path, entry->name);
                        }
                    }
                    filemanager_navigate(fm, new_path);
                } else {
                    /* View file contents */
                    filemanager_view_file(fm, entry->name);
                }
            } else {
                fm->selected_index = clicked_index;
            }
        }
    }

    window_invalidate(win);
}

/**
 * Handle close
 */
static void filemanager_close(window_t *win)
{
    filemanager_t *fm = (filemanager_t *)win->user_data;

    wm_unregister_window(win);
    window_destroy(win);

    if (fm) {
        kfree(fm);
    }
}

/* ============================================================================
 * End of filemanager.c
 * ============================================================================ */
