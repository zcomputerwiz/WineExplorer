/*
 * View as a slide show
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

#ifndef __WINE_EXPLORER_SLIDESHOW_H
#define __WINE_EXPLORER_SLIDESHOW_H

#include <windows.h>

/* full screen slide show of the pictures in folder, from first (or the first picture) */
void slideshow_start(const WCHAR *folder, const WCHAR *first);

/* a picture made to fit max_width x max_height (never larger than it is), and its own size; NULL if it is not one */
HBITMAP picture_preview(const WCHAR *path, int max_width, int max_height, UINT *width, UINT *height);

#endif
