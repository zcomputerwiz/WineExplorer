/*
 * WineExplorer Desktop Environment - Display Properties Monitor Preview Control
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

#ifndef __WINE_DESKCPL_PREVIEW_H
#define __WINE_DESKCPL_PREVIEW_H

#include <windows.h>

typedef struct _PREVIEW_DATA
{
    COLORREF cr_desktop;
    COLORREF cr_active_caption;
    COLORREF cr_active_text;
    COLORREF cr_inactive_caption;
    COLORREF cr_inactive_text;
    COLORREF cr_btn_face;
    COLORREF cr_btn_text;
    COLORREF cr_window;
    COLORREF cr_window_text;
    COLORREF cr_menu;
    COLORREF cr_menu_text;

    WCHAR theme_path[MAX_PATH];
    WCHAR color_name[64];
    WCHAR size_name[64];
    WCHAR wallpaper_path[MAX_PATH];
    int font_size;
    int      wallpaper_style;
} PREVIEW_DATA;

int preview_find_files(const WCHAR *extension, WCHAR paths[][MAX_PATH], int limit);
void preview_init_data(PREVIEW_DATA *data);
void preview_theme_data(PREVIEW_DATA *data, const WCHAR *path);
void preview_expand_path(const WCHAR *src, WCHAR *dst);
void preview_register_class(HINSTANCE hinst);
void preview_unregister_class(HINSTANCE hinst);
void preview_update(HWND hwnd_preview, const PREVIEW_DATA *data);

#endif /* __WINE_DESKCPL_PREVIEW_H */
