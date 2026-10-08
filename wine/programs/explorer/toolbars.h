/*
 * WineExplorer Desktop Environment - taskbar toolbars
 *
 * Quick Launch, Address, Links and Desktop toolbars that sit on the taskbar next to the task buttons.
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

#ifndef __WINE_EXPLORER_TOOLBARS_H
#define __WINE_EXPLORER_TOOLBARS_H

#include <windows.h>

/* the four toolbars of Windows, then the ones the user added ("New Toolbar...") */
#define TOOLBAR_CUSTOM_MAX 4
enum { TOOLBAR_QUICKLAUNCH, TOOLBAR_ADDRESS, TOOLBAR_LINKS, TOOLBAR_DESKTOP, TOOLBAR_CUSTOM,
       TOOLBAR_COUNT = TOOLBAR_CUSTOM + TOOLBAR_CUSTOM_MAX };

/* The toolbars the user added: the number of the slot is the kind - TOOLBAR_CUSTOM */
BOOL toolbars_custom_name(int kind, WCHAR *name, int len);   /* FALSE if the slot is free */
BOOL toolbars_add_custom(HWND tray, const WCHAR *folder);    /* TRUE if there was a free slot */
void toolbars_choose_new(HWND tray);                         /* the "New Toolbar..." dialog */

/* Create the windows of every enabled toolbar as children of the taskbar */
void toolbars_create(HWND tray);
void toolbars_destroy(void);

/* Visibility is stored in the taskbar settings; changing it creates or destroys the window */
BOOL toolbars_is_visible(int kind);
void toolbars_set_visible(HWND tray, int kind, BOOL visible);

/* Place the visible toolbars: Quick Launch starts at x, the others follow the task buttons.  Returns where the
 * task buttons start and how wide they are. */
void toolbars_layout(HWND tray, int x, int tray_left, int height, int *task_x, int *task_w);

/* Called when the visual style, colors or the folders behind the toolbars change */
void toolbars_refresh(void);

void show_desktop_toggle(void);
BOOL toolbars_desktop_shown(void);

#endif /* __WINE_EXPLORER_TOOLBARS_H */
