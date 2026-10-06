/*
 * WineExplorer Desktop Environment - Tray Volume Slider Popup Header
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Header for the authentic Windows XP / Server 2003 vertical master volume
 * slider popup window displayed upon single-clicking the volume tray icon.
 */

#ifndef __WINE_EXPLORER_TRAYVOL_DIALOG_H
#define __WINE_EXPLORER_TRAYVOL_DIALOG_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Show or toggle the tray volume slider popup dialog */
void trayvol_toggle(HWND hwnd_tray_icon, POINT pt_cursor);

/* Dismiss the dialog if visible */
void trayvol_dismiss(void);

/* Check if dialog is currently visible */
BOOL trayvol_is_visible(void);

#ifdef __cplusplus
}
#endif

#endif /* __WINE_EXPLORER_TRAYVOL_DIALOG_H */
