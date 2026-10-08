/*
 * WineExplorer Desktop Environment - System Tray, Clock & Applets
 *
 * Implements:
 * - Sunken notification area in Classic mode & themed notification area in Visual Styles mode
 * - Locale-formatted tray clock with 1-second update timer
 * - Interactive calendar popup & Date/Time control panel launch
 * - "Show Desktop" (Minimize All / Restore) toggle button
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

#endif /* __WINE_EXPLORER_TRAY_CLOCK_H */
