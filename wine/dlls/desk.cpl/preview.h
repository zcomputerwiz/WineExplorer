/*
 * WineExplorer Desktop Environment - Display Properties Monitor Preview Control
 */

#ifndef __WINE_DESKCPL_PREVIEW_H
#define __WINE_DESKCPL_PREVIEW_H

#include <windows.h>

typedef struct _PREVIEW_DATA
{
    COLORREF cr_desktop;
    COLORREF cr_active_caption;
    COLORREF cr_active_text;
    COLORREF cr_inactive_caption;
    COLORREF cr_inactive_text;
    COLORREF cr_btn_face;
    COLORREF cr_btn_text;
    COLORREF cr_window;
    COLORREF cr_window_text;
    COLORREF cr_menu;
    COLORREF cr_menu_text;

    HBITMAP  hbm_wallpaper;
    int      wallpaper_style;
} PREVIEW_DATA;

void preview_register_class(HINSTANCE hinst);
void preview_unregister_class(HINSTANCE hinst);
void preview_update(HWND hwnd_preview, const PREVIEW_DATA *data);

#endif /* __WINE_DESKCPL_PREVIEW_H */
