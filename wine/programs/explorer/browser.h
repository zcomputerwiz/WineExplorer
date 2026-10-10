/*
 * WineExplorer Desktop Environment - Explorer window (menus, toolbars, address bar, status bar)
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

#ifndef __WINE_EXPLORER_BROWSER_H
#define __WINE_EXPLORER_BROWSER_H

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <commoncontrols.h>

struct search_ui;

typedef struct
{
    IExplorerBrowser *browser;
    HWND main_window, path_box;
    INT rebar_height;
    LPITEMIDLIST pidl;
    IImageList *icon_list;
    DWORD advise_cookie;

    IShellWindows *sw;
    LONG sw_cookie;

    /* XP style chrome */
    HWND rebar, toolbar, address_band, brand, status, taskpane;
    HIMAGELIST tb_normal, tb_hot;
    BOOL show_toolbar, show_address, show_status, show_folders;
    BOOL navigating;
    struct search_ui *search;   /* Search Companion and Search Results, when searching */
    BOOL search_started;         /* the window opened as Search Results: its first navigation keeps the results */
    UINT brand_frame;
    int view_mode;
} explorer_info;

enum
{
    BACK_BUTTON = 100, FORWARD_BUTTON, UP_BUTTON, SEARCH_BUTTON, FOLDERS_BUTTON, VIEWS_BUTTON, GO_BUTTON,

    IDM_FILE_NEWFOLDER = 200, IDM_FILE_DELETE, IDM_FILE_RENAME, IDM_FILE_PROPERTIES, IDM_FILE_CLOSE,
    IDM_EDIT_CUT, IDM_EDIT_COPY, IDM_EDIT_PASTE, IDM_EDIT_SELECTALL, IDM_EDIT_INVERT,
    IDM_VIEW_STANDARD, IDM_VIEW_ADDRESS, IDM_VIEW_STATUS, IDM_VIEW_FOLDERS,
    IDM_VIEW_THUMBNAILS, IDM_VIEW_TILES, IDM_VIEW_ICONS, IDM_VIEW_LIST, IDM_VIEW_DETAILS,
    IDM_VIEW_REFRESH, IDM_GO_BACK, IDM_GO_FORWARD, IDM_GO_UP,
    IDM_TOOLS_FOLDEROPTIONS, IDM_HELP_ABOUT,
    IDM_EDIT_UNDO, IDM_EDIT_PASTELINK, IDM_EDIT_COPYTO, IDM_EDIT_MOVETO,
    IDM_FAVORITES_FIRST = 400   /* one id per entry of the Favorites folder */
};

#define IDT_BROWSER_STATUS 1
#define IDT_BROWSER_BRAND  2

#define TASKPANE_WIDTH 190

HWND taskpane_create(explorer_info *info);
void taskpane_update(explorer_info *info);

HMENU browser_create_menu(void);
void browser_create_bars(explorer_info *info, const WCHAR *address_label);
void browser_layout(explorer_info *info, int width, int height);
void browser_command(explorer_info *info, UINT id);
void browser_update_ui(explorer_info *info);
void browser_search(explorer_info *info, BOOL show);   /* the Search button: Search Companion on or off */
void browser_exit_search(explorer_info *info);         /* back to the folder */
void browser_navigation_state(explorer_info *info, BOOL navigating);
void browser_init_menu_popup(explorer_info *info, HMENU menu);
LRESULT browser_toolbar_notify(explorer_info *info, NMHDR *nm);

#endif /* __WINE_EXPLORER_BROWSER_H */
