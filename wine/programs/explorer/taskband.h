/*
 * WineExplorer Desktop Environment - TaskBand (Window Management)
 *
 * Implements running application window tracking, taskbar buttons, active/flashing states,
 * window activation/minimization, grouping, and authentic system menus.
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
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
    BOOL        hot;       /* mouse is over the button */
    BOOL        pressed;   /* left button held down on it */
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
BOOL taskband_is_task_window(HWND hwnd);   /* the windows with a taskbar button (also Alt+Tab's list) */

void taskband_relayout(void);
LRESULT taskband_handle_shell_hook(WPARAM wparam, LPARAM lparam);

#endif /* __WINE_EXPLORER_TASKBAND_H */
