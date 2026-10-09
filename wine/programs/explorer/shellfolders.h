/*
 * WineExplorer Desktop Environment - special folders of the shell namespace
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

#ifndef __WINE_EXPLORER_SHELLFOLDERS_H
#define __WINE_EXPLORER_SHELLFOLDERS_H

#include <windows.h>
#include <shlobj.h>

/* Special folders are addressed by CSIDL and handled through their item ID lists, the way Windows and ReactOS do it:
 * the name comes from the namespace (the class's LocalizedString), the icon from the class's DefaultIcon (which a
 * theme replaces) and opening goes through ShellExecuteEx. */

/* Open the folder; explore = with the Folders pane */
void shellfolder_open(int csidl, BOOL explore);

/* Display name ("My Computer"); FALSE if the folder does not exist */
BOOL shellfolder_name(int csidl, WCHAR *name, int len);

/* Icon at the size (16 or 32), state is the DefaultIcon value ("empty"/"full" for the Recycle Bin, NULL for the
 * default); the caller destroys it */
HICON shellfolder_icon(int csidl, const WCHAR *state, int size);

/* The class of a virtual folder ("{20D04FE0-...}"), NULL for others */
const WCHAR *shellfolder_clsid(int csidl);

/* The namespace items of the desktop in Windows' order (My Documents, My Computer, My Network Places, Recycle Bin,
 * Internet Explorer), without those hidden in "Desktop Items" (HideDesktopIcons).  Returns the count. */
int shellfolder_desktop_items(int *csidls, int max);

/* Whether the folder shows on the desktop, and the "Show on Desktop" command of the Start menu */
BOOL shellfolder_on_desktop(int csidl);
void shellfolder_set_on_desktop(int csidl, BOOL show);

#endif
