/*
 * WineExplorer Desktop Environment - instant search (the taskbar search box of later Windows)
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

#ifndef __WINE_EXPLORER_INSTANT_SEARCH_H
#define __WINE_EXPLORER_INSTANT_SEARCH_H

#include <windows.h>

/* The text of the search box changed: show what matches it above the box (programs, Control Panel, files) */
void instant_search_update(HWND edit, const WCHAR *text);

/* Up, Down, Enter and Escape in the search box while the results are up; TRUE if the key was used */
BOOL instant_search_key(UINT vk);

void instant_search_hide(void);
BOOL instant_search_owns(HWND hwnd);   /* the results window, which focus may move to without closing it */

#endif
