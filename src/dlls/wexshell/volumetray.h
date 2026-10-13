/*
 * WineExplorer Desktop Environment - Volume System Tray Applet Header
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Header for the authentic Windows XP / Server 2003 volume notification tray applet.
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

#ifndef __WINE_EXPLORER_VOLUMETRAY_H
#define __WINE_EXPLORER_VOLUMETRAY_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the volume system tray applet and register notification icon */
BOOL volumetray_init(HWND hwnd_tray);

/* Remove the volume system tray notification icon and clean up resources */
void volumetray_shutdown(void);

/* Force immediate update of the volume tray icon and tooltip */
void volumetray_update(void);

#ifdef __cplusplus
}
#endif

#endif /* __WINE_EXPLORER_VOLUMETRAY_H */
