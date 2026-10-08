/*
 * WineExplorer Desktop Environment - System Tray, Clock & Applets
 *
 * Implements:
 * - Sunken notification area in Classic mode & themed notification area in Visual Styles mode
 * - Locale-formatted tray clock with 1-second update timer
 * - Interactive calendar popup & Date/Time control panel launch
 * - "Show Desktop" (Minimize All / Restore) toggle button
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

#ifndef __WINE_EXPLORER_TRAY_CLOCK_H
#define __WINE_EXPLORER_TRAY_CLOCK_H

#include <windows.h>

#define TRAY_CLOCK_CLASS L"TrayClockWClass"
#define SHOW_DESKTOP_CLASS L"ShowDesktopButtonClass"

/* Tray Clock API */
HWND tray_clock_create(HWND parent_tray, int x, int y, int width, int height);
void tray_clock_destroy(void);
void tray_clock_resize(int x, int y, int width, int height);
void tray_clock_update(void);

/* Show Desktop Button API */
HWND show_desktop_button_create(HWND parent_tray, int x, int y, int width, int height);
void show_desktop_button_destroy(void);
void show_desktop_button_resize(int x, int y, int width, int height);
void show_desktop_toggle(void);
int show_desktop_button_get_width(void); /* quick launch toolbar width */

#endif /* __WINE_EXPLORER_TRAY_CLOCK_H */
