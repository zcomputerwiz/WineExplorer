/*
 * WineExplorer Desktop Environment - Network System Tray Notification Widget Header
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 */

#ifndef __WINE_NETTRAY_H
#define __WINE_NETTRAY_H

#include <windows.h>

BOOL nettray_init(HWND hwnd_tray);
void nettray_shutdown(void);
void nettray_update(void);
void nettray_update_visibility(void);

#endif /* __WINE_NETTRAY_H */
