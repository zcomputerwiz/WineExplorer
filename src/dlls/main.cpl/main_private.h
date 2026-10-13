/*
 * WineExplorer Desktop Environment - main.cpl Private Definitions
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

#ifndef __WINE_MAIN_CPL_PRIVATE_H
#define __WINE_MAIN_CPL_PRIVATE_H

#define OEMRESOURCE
#include <windows.h>
#include <commctrl.h>
#include <cpl.h>
#include <stdio.h>
#include <stdlib.h>

#include "resource.h"

extern HINSTANCE g_hinst;

LONG mouse_applet(HWND parent);
LONG keyboard_applet(HWND parent);

INT_PTR CALLBACK button_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
INT_PTR CALLBACK pointer_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
INT_PTR CALLBACK motion_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
INT_PTR CALLBACK wheel_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
INT_PTR CALLBACK mouse_hardware_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

INT_PTR CALLBACK keyboard_speed_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
INT_PTR CALLBACK keyboard_hardware_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

INT_PTR CALLBACK clicklock_settings_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

#endif /* __WINE_MAIN_CPL_PRIVATE_H */
