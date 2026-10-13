/*
 * WineExplorer Desktop Environment - Taskbar and Start Menu Properties Header
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

#ifndef __WINE_EXPLORER_TASKBAR_PROPS_H
#define __WINE_EXPLORER_TASKBAR_PROPS_H

#include <windows.h>

void taskbar_props_show(HWND parent);

/* Active Taskbar Settings */
typedef struct _TASKBAR_SETTINGS
{
    BOOL lock_taskbar;
    BOOL autohide;
    BOOL always_on_top;
    BOOL group_buttons;
    BOOL show_quick_launch;
    BOOL show_address;
    BOOL show_links;
    BOOL show_desktop_toolbar;
    BOOL show_search;          /* the instant search box (not in Windows XP) */
    BOOL show_clock;
    BOOL hide_inactive_icons;
    int  start_menu_style; /* 0: Classic, 1: Two-column XP, 2: Modern */
} TASKBAR_SETTINGS;

TASKBAR_SETTINGS *taskbar_settings_get(void);
void taskbar_settings_save(void);
void taskbar_settings_load(void);

#endif /* __WINE_EXPLORER_TASKBAR_PROPS_H */
