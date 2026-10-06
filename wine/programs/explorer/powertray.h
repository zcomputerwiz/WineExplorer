/*
 * WineExplorer Desktop Environment - Power & Battery Tray Applet Header
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Header for the authentic Windows XP / Server 2003 battery and power
 * notification tray applet.
 */

#ifndef __WINE_EXPLORER_POWERTRAY_H
#define __WINE_EXPLORER_POWERTRAY_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the battery/power tray applet */
BOOL powertray_init(HWND hwnd_tray);

/* Remove tray icon and clean up resources */
void powertray_shutdown(void);

/* Force immediate update of power status and tooltip */
void powertray_update(void);

#ifdef __cplusplus
}
#endif

#endif /* __WINE_EXPLORER_POWERTRAY_H */
