/*
 * WineExplorer Desktop Environment - Flexible Start Menu
 *
 * Implements multi-style Start Menu engine:
 * 1. Classic Server 2003 / NT single-column cascading menu with vertical banner
 * 2. Windows XP / 2003 two-column layout (pinned/recent on left, shell folders on right)
 * 3. Open-Shell inspired modern hybrid layout with integrated search box
 * Authentic Start button with Windows flag icon and theme-aware rendering.
 */

#ifndef __WINE_EXPLORER_STARTMENU_VIEW_H
#define __WINE_EXPLORER_STARTMENU_VIEW_H

#include <windows.h>
#include <shlobj.h>

#define STARTMENU_CMD_RUN         1001
#define STARTMENU_CMD_SHUTDOWN    1002
#define STARTMENU_CMD_LOGOFF      1003
#define STARTMENU_CMD_CONTROLPANEL 1004
#define STARTMENU_CMD_PRINTERS    1005
#define STARTMENU_CMD_HELP        1006
#define STARTMENU_CMD_SEARCH      1007
#define STARTMENU_CMD_MYDOCS      1008
#define STARTMENU_CMD_MYCOMP      1009
#define STARTMENU_CMD_MYNET       1010
#define STARTMENU_CMD_PICTURES    1011
#define STARTMENU_CMD_MUSIC       1012
#define STARTMENU_CMD_CONNECT_TO  1013
#define STARTMENU_CMD_NETWORK     1014

/* Start Menu API */
void startmenu_view_show(HWND tray_hwnd, const RECT *start_btn_rect);
void startmenu_view_hide(void);
BOOL startmenu_view_is_open(void);

/* Start Button API */
HWND startbutton_create(HWND parent_tray, int x, int y, int width, int height);
void startbutton_destroy(void);
void startbutton_resize(int x, int y, int width, int height);

#endif /* __WINE_EXPLORER_STARTMENU_VIEW_H */
