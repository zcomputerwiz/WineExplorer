/*
 * WineExplorer Desktop Environment - Volume System Tray Applet Header
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Header for the authentic Windows XP / Server 2003 volume notification tray applet.
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
