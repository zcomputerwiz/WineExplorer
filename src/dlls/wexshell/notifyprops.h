/*
 * WineExplorer Desktop Environment - Customize Notifications
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

#ifndef __WINE_EXPLORER_NOTIFYPROPS_H
#define __WINE_EXPLORER_NOTIFYPROPS_H

#include <windows.h>

/* how an icon of the notification area behaves (the entries of the combo box of the dialog) */
#define NOTIFY_HIDE_WHEN_INACTIVE 0
#define NOTIFY_ALWAYS_HIDE        1
#define NOTIFY_ALWAYS_SHOW        2

typedef struct _TRAY_ICON_INFO
{
    WCHAR key[300];     /* program and id of the icon: how the behavior is stored */
    WCHAR text[128];
    HICON icon;         /* a copy, the caller destroys it */
    int   behavior;
} TRAY_ICON_INFO;

/* systray.c */
int  systray_enum_icons(TRAY_ICON_INFO *out, int max);
void systray_set_behavior(const WCHAR *key, int behavior);
void systray_reset_behaviors(void);

void notifyprops_show(HWND parent);

#endif
