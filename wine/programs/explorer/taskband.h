/*
 * WineExplorer Desktop Environment - TaskBand (Window Management)
 *
 * Implements running application window tracking, taskbar buttons, active/flashing states,
 * window activation/minimization, grouping, and authentic system menus.
 */

#ifndef __WINE_EXPLORER_TASKBAND_H
#define __WINE_EXPLORER_TASKBAND_H

#include <windows.h>

typedef struct _TASK_ITEM
{
    HWND        target_hwnd;
    HWND        button_hwnd;
    HICON       icon;
    WCHAR       title[256];
    DWORD       process_id;
    BOOL        active;
    BOOL        flashing;
    BOOL        minimized;
    BOOL        seen;
    RECT        rect;
    struct _TASK_ITEM *next;
} TASK_ITEM;

typedef struct _TASKBAND
{
    HWND hwnd;
    HWND parent_tray;
    TASK_ITEM *tasks;
    int task_count;

    int button_width;
    int button_height;
    int max_width;

    BOOL group_similar;
    UINT shell_hook_msg;
} TASKBAND;

/* TaskBand API */
HWND taskband_create(HWND parent_tray, int x, int y, int width, int height);
void taskband_destroy(void);

void taskband_resize(int x, int y, int width, int height);
void taskband_add_window(HWND hwnd);
void taskband_remove_window(HWND hwnd);
void taskband_activate_window(HWND hwnd);
void taskband_update_window(HWND hwnd);
void taskband_flash_window(HWND hwnd);
void taskband_sync_windows(void);

void taskband_relayout(void);
LRESULT taskband_handle_shell_hook(WPARAM wparam, LPARAM lparam);

#endif /* __WINE_EXPLORER_TASKBAND_H */
