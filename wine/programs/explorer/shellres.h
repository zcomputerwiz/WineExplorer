/*
 * WineExplorer Desktop Environment - shell resources (icons and bitmaps by Windows resource id)
 *
 * Icons and bitmaps are addressed by (module, id) like on Windows (shell32 id 32 = Recycle Bin ...).  If a genuine
 * it; otherwise it uses Wine's shell32 and the bitmaps built into explorer.exe (ReactOS).
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

#ifndef __WINE_EXPLORER_SHELLRES_H
#define __WINE_EXPLORER_SHELLRES_H

#include <windows.h>

/* shell32 icon by resource id at the requested size; the caller destroys it.  NULL if neither set has it. */
HICON shellres_icon(int id, int size);

/* An icon of any module of the genuine pack by resource id; NULL without pack */
HICON shellres_module_icon(const WCHAR *module, int id, int size);

/* Icon of a program (by executable name) from the theme's icon pack Apps folder; NULL if the pack has none */
HICON shellres_app_icon(const WCHAR *exe, int size);

/* Bitmap by (module, id) from the genuine pack, else from explorer.exe's resource fallback_id (0 = none).  Caller
 * deletes it. */
HBITMAP shellres_bitmap(const WCHAR *module, int id, int fallback_id);

/* Make the pixels of a 32 bit bitmap with straight alpha premultiplied (what AlphaBlend wants); a bitmap that stores
 * no alpha at all becomes opaque. */
void shellres_premultiply(HBITMAP bitmap);

/* A bitmap of the visual style's shellstyle.dll (Luna's Shell\<color>\shellstyle.dll): the task group icons and the
 * folder watermarks of Windows XP.  NULL with Windows Classic or without the genuine theme.  Caller deletes it. */
HBITMAP shellres_style_bitmap(int id);

/* The XP Go button of an address bar (a one-button toolbar sending WM_COMMAND id to parent), and its width */
HWND shellres_go_button(HWND parent, UINT id);
int shellres_go_width(HWND go);

/* Drop the cached packs after the theme changed */
void shellres_reset(void);

/* TRUE if a genuine pack is installed for the module */
BOOL shellres_has_pack(const WCHAR *module);

/* Point shell32's own icons (Shell Icons key) at the theme's pack, or back at shell32; TRUE if that changed anything */
BOOL shellres_sync_shell_icons(void);

/* Reload shell32's icons in this process's system image list after Shell Icons changed */
void shellres_reload_shell_icons(void);

#endif
