/*
 * WineExplorer Desktop Environment - timedate.cpl Internal Header
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

#ifndef __WINE_TIMEDATE_H
#define __WINE_TIMEDATE_H

#include <windows.h>
#include <commctrl.h>

#include "resource.h"

extern HINSTANCE g_hInstance;

void clock_register_class(HINSTANCE hinst);
void clock_unregister_class(HINSTANCE hinst);

INT_PTR CALLBACK dateandtime_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
INT_PTR CALLBACK timezone_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);


#define CLM_STOPCLOCK  (WM_USER + 1)
#define CLM_STARTCLOCK (WM_USER + 2)
#define CLM_SETTIME    (WM_USER + 3)

#endif /* __WINE_TIMEDATE_H */
