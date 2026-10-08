/*
 * WineExplorer Desktop Environment - Explorer window chrome
 *
 * Menu bar, Standard Buttons toolbar, Address bar with Go, activity logo and status bar around the shell view
 * (IExplorerBrowser).  Commands act on the current shell view through IShellView, IFolderView and the shell's own
 * context menus, so the file operations are the shell's.
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

#define COBJMACROS

#include <stdio.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <commctrl.h>

#include "wine/debug.h"
#include "resource.h"
#include "browser.h"
#include "shellres.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

#define ADDRESS_CLASS L"BrowserAddressBand"
#define BRAND_CLASS   L"BrowserBrand"
#define TB_ICON       24

/* positions in the toolbar picture strip */
enum { IMG_BACK = 0, IMG_FORWARD = 1, IMG_SEARCH = 17, IMG_FOLDERS = 21, IMG_VIEWS = 20, IMG_UP = 28, IMG_GO = 33 };

extern HINSTANCE explorer_hInstance;

/* ---- helpers on the current view ---- */

static IShellView *current_view(explorer_info *info)
{
    IShellView *view = NULL;
    IExplorerBrowser_GetCurrentView(info->browser, &IID_IShellView, (void **)&view);
    return view;
}

static IFolderView *current_folder_view(explorer_info *info)
{
    IFolderView *view = NULL;
    IExplorerBrowser_GetCurrentView(info->browser, &IID_IFolderView, (void **)&view);
    return view;
}

static void invoke_verb(explorer_info *info, int which, const char *verb)
{
    IShellView *view = current_view(info);
    IContextMenu *menu = NULL;

    if (!view) return;
    if (SUCCEEDED(IShellView_GetItemObject(view, which, &IID_IContextMenu, (void **)&menu)) && menu)
    {
        HMENU popup = CreatePopupMenu();
        if (popup && SUCCEEDED(IContextMenu_QueryContextMenu(menu, popup, 0, 1, 0x7fff, CMF_NORMAL | CMF_CANRENAME)))
        {
            CMINVOKECOMMANDINFO ici = { sizeof(ici), 0, info->main_window, verb, NULL, NULL, SW_SHOWNORMAL };
            IContextMenu_InvokeCommand(menu, &ici);
        }
        if (popup) DestroyMenu(popup);
        IContextMenu_Release(menu);
    }
    IShellView_Release(view);
}

static HWND view_list(explorer_info *info);

/* Wine's IFolderView does not count selections, so ask the list control of the view */
static int item_count(explorer_info *info, UINT which)
{
    HWND list = view_list(info);

    if (!list) return 0;
    return (int)SendMessageW(list, which == SVGIO_SELECTION ? LVM_GETSELECTEDCOUNT : LVM_GETITEMCOUNT, 0, 0);
}

static void set_view_mode(explorer_info *info, UINT mode)
{
    IFolderView *fv = current_folder_view(info);
    if (!fv) return;
    IFolderView_SetCurrentViewMode(fv, mode);
    IFolderView_Release(fv);
    info->view_mode = mode;
}

static UINT get_view_mode(explorer_info *info)
{
    IFolderView *fv = current_folder_view(info);
    UINT mode = FVM_DETAILS;
    if (fv)
    {
        IFolderView_GetCurrentViewMode(fv, &mode);
        IFolderView_Release(fv);
    }
    return mode;
}

/* The shell view's list control (Details, List, Icons ...) */
static HWND view_list(explorer_info *info)
{
    IShellView *sv = current_view(info);
    HWND view = NULL, list = NULL;

    if (sv)
    {
        if (SUCCEEDED(IShellView_GetWindow(sv, &view))) list = FindWindowExW(view, NULL, L"SysListView32", NULL);
        IShellView_Release(sv);
    }
    return list;
}

static void change_selection(explorer_info *info, BOOL invert)
{
    HWND list = view_list(info);
    int i, count;

    if (!list) return;
    count = (int)SendMessageW(list, LVM_GETITEMCOUNT, 0, 0);
    for (i = 0; i < count; i++)
    {
        LVITEMW item = { .stateMask = LVIS_SELECTED };
        BOOL selected = SendMessageW(list, LVM_GETITEMSTATE, i, LVIS_SELECTED) & LVIS_SELECTED;
        item.state = (invert ? !selected : TRUE) ? LVIS_SELECTED : 0;
        SendMessageW(list, LVM_SETITEMSTATE, i, (LPARAM)&item);
    }
}

#define select_all(info) change_selection((info), FALSE)
#define invert_selection(info) change_selection((info), TRUE)

static void new_folder(explorer_info *info)
{
    WCHAR dir[MAX_PATH], path[MAX_PATH];
    int n = 1;
    LPITEMIDLIST pidl;
    IShellView *sv;

    if (!SHGetPathFromIDListW(info->pidl, dir)) return;   /* only real folders can hold a new folder */
    swprintf(path, MAX_PATH, L"%s\\New Folder", dir);
    while (PathFileExistsW(path)) swprintf(path, MAX_PATH, L"%s\\New Folder (%d)", dir, ++n);
    if (!CreateDirectoryW(path, NULL)) return;

    SHChangeNotify(SHCNE_MKDIR, SHCNF_PATHW, path, NULL);
    if ((sv = current_view(info)))
    {
        IShellView_Refresh(sv);
        if ((pidl = ILCreateFromPathW(path)))
        {
            IShellView_SelectItem(sv, ILFindLastID(pidl), SVSI_SELECT | SVSI_EDIT | SVSI_ENSUREVISIBLE | SVSI_FOCUSED | SVSI_DESELECTOTHERS);
            ILFree(pidl);
        }
        IShellView_Release(sv);
    }
}

/* ---- menu ---- */

static HMENU g_favorites_menu;

HMENU browser_create_menu(void)
{
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), edit = CreatePopupMenu(), view = CreatePopupMenu();
    HMENU favorites = CreatePopupMenu(), tools = CreatePopupMenu(), help = CreatePopupMenu();
    HMENU new_menu = CreatePopupMenu(), toolbars = CreatePopupMenu(), bar_menu = CreatePopupMenu(), goto_menu = CreatePopupMenu();

    AppendMenuW(new_menu, MF_STRING, IDM_FILE_NEWFOLDER, L"&Folder");
    AppendMenuW(file, MF_POPUP, (UINT_PTR)new_menu, L"&New");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, IDM_FILE_DELETE, L"&Delete");
    AppendMenuW(file, MF_STRING, IDM_FILE_RENAME, L"Rena&me");
    AppendMenuW(file, MF_STRING, IDM_FILE_PROPERTIES, L"P&roperties");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, IDM_FILE_CLOSE, L"&Close");

    AppendMenuW(edit, MF_STRING, IDM_EDIT_CUT, L"Cu&t\tCtrl+X");
    AppendMenuW(edit, MF_STRING, IDM_EDIT_COPY, L"&Copy\tCtrl+C");
    AppendMenuW(edit, MF_STRING, IDM_EDIT_PASTE, L"&Paste\tCtrl+V");
    AppendMenuW(edit, MF_SEPARATOR, 0, NULL);
    AppendMenuW(edit, MF_STRING, IDM_EDIT_SELECTALL, L"Select &All\tCtrl+A");
    AppendMenuW(edit, MF_STRING, IDM_EDIT_INVERT, L"&Invert Selection");

    AppendMenuW(toolbars, MF_STRING, IDM_VIEW_STANDARD, L"&Standard Buttons");
    AppendMenuW(toolbars, MF_STRING, IDM_VIEW_ADDRESS, L"&Address Bar");
    AppendMenuW(view, MF_POPUP, (UINT_PTR)toolbars, L"&Toolbars");
    AppendMenuW(view, MF_STRING, IDM_VIEW_STATUS, L"Status &Bar");
    AppendMenuW(bar_menu, MF_STRING, IDM_VIEW_FOLDERS, L"F&olders");
    AppendMenuW(view, MF_POPUP, (UINT_PTR)bar_menu, L"E&xplorer Bar");
    AppendMenuW(view, MF_SEPARATOR, 0, NULL);
    AppendMenuW(view, MF_STRING, IDM_VIEW_THUMBNAILS, L"Th&umbnails");
    AppendMenuW(view, MF_STRING, IDM_VIEW_TILES, L"Tile&s");
    AppendMenuW(view, MF_STRING, IDM_VIEW_ICONS, L"Ico&ns");
    AppendMenuW(view, MF_STRING, IDM_VIEW_LIST, L"Lis&t");
    AppendMenuW(view, MF_STRING, IDM_VIEW_DETAILS, L"&Details");
    AppendMenuW(view, MF_SEPARATOR, 0, NULL);
    AppendMenuW(goto_menu, MF_STRING, IDM_GO_BACK, L"&Back\tAlt+Left Arrow");
    AppendMenuW(goto_menu, MF_STRING, IDM_GO_FORWARD, L"&Forward\tAlt+Right Arrow");
    AppendMenuW(goto_menu, MF_STRING, IDM_GO_UP, L"&Up One Level");
    AppendMenuW(view, MF_POPUP, (UINT_PTR)goto_menu, L"&Go To");
    AppendMenuW(view, MF_STRING, IDM_VIEW_REFRESH, L"&Refresh\tF5");

    AppendMenuW(tools, MF_STRING | MF_GRAYED, IDM_TOOLS_FOLDEROPTIONS, L"&Folder Options...");
    AppendMenuW(help, MF_STRING, IDM_HELP_ABOUT, L"&About Windows");

    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit, L"&Edit");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, L"&View");
    g_favorites_menu = favorites;
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)favorites, L"F&avorites");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)tools, L"&Tools");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");
    return bar;
}

static void fill_favorites(explorer_info *info, HMENU menu);

/* Fill the Favorites menu from the user's Favorites folder when it opens, tick and grey the other menus' items */
void browser_init_menu_popup(explorer_info *info, HMENU menu)
{
    static const UINT view_ids[] = { IDM_VIEW_THUMBNAILS, IDM_VIEW_TILES, IDM_VIEW_ICONS, IDM_VIEW_LIST, IDM_VIEW_DETAILS };
    static const UINT view_modes[] = { FVM_THUMBNAIL, FVM_TILE, FVM_ICON, FVM_LIST, FVM_DETAILS };
    int i;
    UINT mode;

    if (menu == g_favorites_menu) { fill_favorites(info, menu); return; }

    if (GetMenuState(menu, IDM_VIEW_STANDARD, MF_BYCOMMAND) != (UINT)-1)
    {
        CheckMenuItem(menu, IDM_VIEW_STANDARD, MF_BYCOMMAND | (info->show_toolbar ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menu, IDM_VIEW_ADDRESS, MF_BYCOMMAND | (info->show_address ? MF_CHECKED : MF_UNCHECKED));
    }
    if (GetMenuState(menu, IDM_VIEW_STATUS, MF_BYCOMMAND) != (UINT)-1)
        CheckMenuItem(menu, IDM_VIEW_STATUS, MF_BYCOMMAND | (info->show_status ? MF_CHECKED : MF_UNCHECKED));
    if (GetMenuState(menu, IDM_VIEW_FOLDERS, MF_BYCOMMAND) != (UINT)-1)
        CheckMenuItem(menu, IDM_VIEW_FOLDERS, MF_BYCOMMAND | (info->show_folders ? MF_CHECKED : MF_UNCHECKED));
    if (GetMenuState(menu, IDM_VIEW_DETAILS, MF_BYCOMMAND) != (UINT)-1)
    {
        mode = get_view_mode(info);
        for (i = 0; i < 5; i++)
            CheckMenuItem(menu, view_ids[i], MF_BYCOMMAND | (view_modes[i] == mode ? MF_CHECKED : MF_UNCHECKED));
    }
    if (GetMenuState(menu, IDM_EDIT_CUT, MF_BYCOMMAND) != (UINT)-1 || GetMenuState(menu, IDM_FILE_DELETE, MF_BYCOMMAND) != (UINT)-1)
    {
        UINT flag = item_count(info, SVGIO_SELECTION) > 0 ? MF_ENABLED : MF_GRAYED;
        for (i = IDM_FILE_DELETE; i <= IDM_FILE_PROPERTIES; i++) EnableMenuItem(menu, i, MF_BYCOMMAND | flag);
        EnableMenuItem(menu, IDM_EDIT_CUT, MF_BYCOMMAND | flag);
        EnableMenuItem(menu, IDM_EDIT_COPY, MF_BYCOMMAND | flag);
    }
}

static void fill_favorites(explorer_info *info, HMENU menu)
{
    WCHAR dir[MAX_PATH], pattern[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE find;
    int n = 0;

    while (GetMenuItemCount(menu) > 0) DeleteMenu(menu, 0, MF_BYPOSITION);
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_FAVORITES, NULL, SHGFP_TYPE_CURRENT, dir)))
    {
        swprintf(pattern, MAX_PATH, L"%s\\*", dir);
        if ((find = FindFirstFileW(pattern, &fd)) != INVALID_HANDLE_VALUE)
        {
            do
            {
                WCHAR title[MAX_PATH], *dot;
                if ((fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN)) || !wcsicmp(fd.cFileName, L"desktop.ini")) continue;
                lstrcpynW(title, fd.cFileName, MAX_PATH);
                if ((dot = wcsrchr(title, '.'))) *dot = 0;
                AppendMenuW(menu, MF_STRING, IDM_FAVORITES_FIRST + n++, title);
            } while (FindNextFileW(find, &fd) && n < 100);
            FindClose(find);
        }
    }
    if (!n) AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"(Empty)");
}

static void open_favorite(explorer_info *info, int index)
{
    WCHAR dir[MAX_PATH], pattern[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE find;
    int n = 0;

    if (FAILED(SHGetFolderPathW(NULL, CSIDL_FAVORITES, NULL, SHGFP_TYPE_CURRENT, dir))) return;
    swprintf(pattern, MAX_PATH, L"%s\\*", dir);
    if ((find = FindFirstFileW(pattern, &fd)) == INVALID_HANDLE_VALUE) return;
    do
    {
        if ((fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN)) || !wcsicmp(fd.cFileName, L"desktop.ini")) continue;
        if (n++ == index)
        {
            WCHAR path[MAX_PATH];
            swprintf(path, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
            ShellExecuteW(info->main_window, NULL, path, NULL, NULL, SW_SHOWNORMAL);
            break;
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

/* ---- address band ---- */

static LRESULT CALLBACK address_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_SIZE:
    {
        HWND combo = GetWindow(hwnd, GW_CHILD);
        HWND go = combo ? GetWindow(combo, GW_HWNDNEXT) : NULL;
        int w = LOWORD(lparam), go_w = 62;
        if (combo) SetWindowPos(combo, NULL, 0, 0, max(20, w - go_w - 4), 240, SWP_NOZORDER | SWP_NOACTIVATE);
        if (go) SetWindowPos(go, NULL, max(20, w - go_w - 2), 0, go_w, HIWORD(lparam), SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_NOTIFY:
    case WM_COMMAND:
        return SendMessageW(GetAncestor(hwnd, GA_ROOT), msg, wparam, lparam);
    case WM_ERASEBKGND:
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wparam, &rc, GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* ---- activity logo ---- */

/* The logo is a strip of square frames; the genuine pack has the 38 pixel one, the built-in one is 22 pixels */
static HBITMAP brand_bitmap(int *size, int *frames)
{
    static HBITMAP cached;
    static int cached_size, cached_frames;

    if (!cached)
    {
        cached = shellres_bitmap(L"shell32", 242, IDB_BROWSER_BRAND);
        if (cached)
        {
            BITMAP bm;
            if (GetObjectW(cached, sizeof(bm), &bm))
            {
                cached_size = bm.bmWidth;
                cached_frames = max(1, abs(bm.bmHeight) / bm.bmWidth);
                if (bm.bmBitsPixel == 32 && bm.bmBits)
                {
                    /* straight alpha, if there is any, becomes premultiplied for AlphaBlend; none means opaque */
                    DWORD *px = bm.bmBits;
                    int i, n = bm.bmWidth * abs(bm.bmHeight);
                    BOOL has_alpha = FALSE;
                    for (i = 0; i < n && !has_alpha; i++) has_alpha = (px[i] >> 24) != 0;
                    if (!has_alpha) for (i = 0; i < n; i++) px[i] |= 0xff000000;
                    else for (i = 0; i < n; i++)
                    {
                        DWORD a = px[i] >> 24;
                        px[i] = (a << 24) | ((((px[i] >> 16) & 255) * a / 255) << 16) | ((((px[i] >> 8) & 255) * a / 255) << 8) | ((px[i] & 255) * a / 255);
                    }
                }
            }
        }
    }
    *size = cached_size;
    *frames = cached_frames;
    return cached;
}

static LRESULT CALLBACK brand_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    explorer_info *info = (explorer_info *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        int size, frames;
        HBITMAP strip = brand_bitmap(&size, &frames);

        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
        if (strip)
        {
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP prev = SelectObject(mem, strip);
            BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            BITMAP bm;
            UINT frame = info && info->navigating ? info->brand_frame % frames : 0;
            int x = (rc.right - size) / 2, y = (rc.bottom - size) / 2;

            GetObjectW(strip, sizeof(bm), &bm);
            if (bm.bmBitsPixel == 32) AlphaBlend(hdc, x, y, size, size, mem, 0, frame * size, size, size, blend);
            else BitBlt(hdc, x, y, size, size, mem, 0, frame * size, SRCCOPY);
            SelectObject(mem, prev);
            DeleteDC(mem);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* ---- bars ---- */

/* The picture strip of the Standard Buttons: the genuine shell32 bitmap if installed, else the built-in one.  Both
 * have the same 47 pictures in the same order. */
static HIMAGELIST load_strip(int shell32_id, int builtin_id)
{
    HBITMAP bmp = shellres_bitmap(L"shell32", shell32_id, builtin_id);
    HIMAGELIST list = NULL;

    if (bmp)
    {
        BITMAP bm;
        GetObjectW(bmp, sizeof(bm), &bm);
        if (bm.bmBitsPixel == 32)
        {
            list = ImageList_Create(TB_ICON, TB_ICON, ILC_COLOR32, bm.bmWidth / TB_ICON, 0);
            ImageList_Add(list, bmp, NULL);
        }
        else
        {
            list = ImageList_Create(TB_ICON, TB_ICON, ILC_COLOR24 | ILC_MASK, bm.bmWidth / TB_ICON, 0);
            ImageList_AddMasked(list, bmp, RGB(0, 0, 0));
        }
        DeleteObject(bmp);
    }
    return list;
}

void browser_create_bars(explorer_info *info, const WCHAR *address_label)
{
    REBARBANDINFOW band = { sizeof(band) };
    WNDCLASSW wc = {0};
    TBBUTTON buttons[8];
    RECT rc;
    int n = 0;

    wc.lpfnWndProc = address_proc;
    wc.hInstance = explorer_hInstance;
    wc.lpszClassName = ADDRESS_CLASS;
    RegisterClassW(&wc);
    wc.lpfnWndProc = brand_proc;
    wc.lpszClassName = BRAND_CLASS;
    RegisterClassW(&wc);

    info->show_toolbar = info->show_address = info->show_status = TRUE;
    info->show_folders = FALSE;

    info->rebar = CreateWindowExW(WS_EX_TOOLWINDOW, REBARCLASSNAMEW, NULL,
                                  WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | RBS_VARHEIGHT | RBS_BANDBORDERS |
                                  CCS_TOP | CCS_NODIVIDER, 0, 0, 0, 0, info->main_window, NULL, explorer_hInstance, NULL);

    /* Standard Buttons */
    info->toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL,
                                    WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TOOLTIPS | TBSTYLE_TRANSPARENT |
                                    CCS_NORESIZE | CCS_NODIVIDER, 0, 0, 0, 0, info->rebar, NULL, explorer_hInstance, NULL);
    SendMessageW(info->toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(info->toolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_DRAWDDARROWS);
    info->tb_normal = load_strip(214, IDB_BROWSER_TB);
    info->tb_hot = load_strip(215, IDB_BROWSER_TB_HOT);
    if (info->tb_normal) SendMessageW(info->toolbar, TB_SETIMAGELIST, 0, (LPARAM)info->tb_normal);
    if (info->tb_hot) SendMessageW(info->toolbar, TB_SETHOTIMAGELIST, 0, (LPARAM)info->tb_hot);

    memset(buttons, 0, sizeof(buttons));
#define BTN(img, id, style, text) do { buttons[n].iBitmap = (img); buttons[n].idCommand = (id); buttons[n].fsState = TBSTATE_ENABLED; \
                                       buttons[n].fsStyle = (style); buttons[n].iString = (INT_PTR)(text); n++; } while (0)
    BTN(IMG_BACK, BACK_BUTTON, BTNS_BUTTON | BTNS_SHOWTEXT | BTNS_AUTOSIZE, L"Back");
    BTN(IMG_FORWARD, FORWARD_BUTTON, BTNS_BUTTON | BTNS_AUTOSIZE, 0);
    BTN(IMG_UP, UP_BUTTON, BTNS_BUTTON | BTNS_AUTOSIZE, 0);
    buttons[n].fsStyle = BTNS_SEP; n++;
    BTN(IMG_SEARCH, SEARCH_BUTTON, BTNS_BUTTON | BTNS_SHOWTEXT | BTNS_AUTOSIZE, L"Search");
    BTN(IMG_FOLDERS, FOLDERS_BUTTON, BTNS_CHECK | BTNS_SHOWTEXT | BTNS_AUTOSIZE, L"Folders");
    buttons[n].fsStyle = BTNS_SEP; n++;
    BTN(IMG_VIEWS, VIEWS_BUTTON, BTNS_WHOLEDROPDOWN | BTNS_AUTOSIZE, 0);
#undef BTN
    {
        int i;
        for (i = 0; i < n; i++)
        {
            if (buttons[i].iString)
            {
                WCHAR buf[32] = {0};
                lstrcpynW(buf, (const WCHAR *)buttons[i].iString, 30);   /* TB_ADDSTRING needs a double-null end */
                buttons[i].iString = SendMessageW(info->toolbar, TB_ADDSTRINGW, 0, (LPARAM)buf);
            }
        }
    }
    SendMessageW(info->toolbar, TB_ADDBUTTONSW, n, (LPARAM)buttons);
    SendMessageW(info->toolbar, TB_AUTOSIZE, 0, 0);

    SendMessageW(info->toolbar, TB_GETITEMRECT, 0, (LPARAM)&rc);
    band.fMask = RBBIM_STYLE | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_SIZE;
    band.fStyle = RBBS_GRIPPERALWAYS | RBBS_CHILDEDGE;
    band.hwndChild = info->toolbar;
    band.cyMinChild = band.cyChild = max(rc.bottom - rc.top, 38) ;
    band.cxMinChild = 0;
    band.cx = 600;
    SendMessageW(info->rebar, RB_INSERTBANDW, -1, (LPARAM)&band);

    /* activity logo, at the right end of the same row */
    {
        int size, frames;
        brand_bitmap(&size, &frames);
        size = max(size, 22);
        info->brand = CreateWindowExW(0, BRAND_CLASS, NULL, WS_CHILD | WS_VISIBLE, 0, 0, size + 18, band.cyChild, info->rebar, NULL, explorer_hInstance, NULL);
        SetWindowLongPtrW(info->brand, GWLP_USERDATA, (LONG_PTR)info);
        band.fStyle = RBBS_FIXEDSIZE | RBBS_NOGRIPPER;
        band.hwndChild = info->brand;
        band.cyMinChild = band.cyChild = max(band.cyChild, size + 4);
        band.cxMinChild = band.cx = size + 18;
    }
    SendMessageW(info->rebar, RB_INSERTBANDW, -1, (LPARAM)&band);

    /* Address bar: label, path box, Go */
    info->address_band = CreateWindowExW(0, ADDRESS_CLASS, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                                         0, 0, 400, 24, info->rebar, NULL, explorer_hInstance, NULL);
    info->path_box = CreateWindowW(WC_COMBOBOXEXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWN, 0, 0, 300, 240,
                                   info->address_band, NULL, explorer_hInstance, NULL);
    {
        HWND go = CreateWindowW(L"BUTTON", L"Go", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT, 0, 0, 60, 22,
                                info->address_band, (HMENU)GO_BUTTON, explorer_hInstance, NULL);
        SendMessageW(go, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE);
    }
    GetWindowRect(info->path_box, &rc);
    band.fMask = RBBIM_STYLE | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_TEXT;
    band.fStyle = RBBS_GRIPPERALWAYS | RBBS_CHILDEDGE | RBBS_BREAK;
    band.lpText = (WCHAR *)address_label;
    band.hwndChild = info->address_band;
    band.cyMinChild = band.cyChild = max(rc.bottom - rc.top, 24);
    band.cxMinChild = 0;
    band.cx = 600;
    SendMessageW(info->rebar, RB_INSERTBANDW, -1, (LPARAM)&band);

    info->taskpane = taskpane_create(info);
    info->status = CreateWindowExW(0, STATUSCLASSNAMEW, NULL, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
                                   info->main_window, NULL, explorer_hInstance, NULL);
    SetTimer(info->main_window, IDT_BROWSER_STATUS, 400, NULL);
    SetTimer(info->main_window, IDT_BROWSER_BRAND, 70, NULL);
}

void browser_layout(explorer_info *info, int width, int height)
{
    RECT rect;
    int status_h = 0;

    if (info->status)
    {
        ShowWindow(info->status, info->show_status ? SW_SHOW : SW_HIDE);
        if (info->show_status)
        {
            int parts[2] = { max(100, width - 180), -1 };
            SendMessageW(info->status, WM_SIZE, 0, 0);
            SendMessageW(info->status, SB_SETPARTS, 2, (LPARAM)parts);
            GetWindowRect(info->status, &rect);
            status_h = rect.bottom - rect.top;
        }
    }
    SetRect(&rect, 0, info->rebar_height, width, max(info->rebar_height, height - status_h));
    if (info->taskpane)
    {
        BOOL pane = !info->show_folders;
        ShowWindow(info->taskpane, pane ? SW_SHOWNA : SW_HIDE);
        if (pane)
        {
            SetWindowPos(info->taskpane, NULL, 0, rect.top, TASKPANE_WIDTH, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
            rect.left = TASKPANE_WIDTH;
        }
    }
    IExplorerBrowser_SetRect(info->browser, NULL, rect);
}

/* ---- status bar ---- */

void browser_update_ui(explorer_info *info)
{
    WCHAR text[200];
    int total, selected;

    if (!info->status) return;
    total = item_count(info, SVGIO_ALLVIEW);
    selected = item_count(info, SVGIO_SELECTION);
    if (selected > 0)
        swprintf(text, ARRAY_SIZE(text), L"%d object%s selected", selected, selected == 1 ? L"" : L"s");
    else
        swprintf(text, ARRAY_SIZE(text), L"%d object%s", total, total == 1 ? L"" : L"s");
    SendMessageW(info->status, SB_SETTEXTW, 0, (LPARAM)text);
    SendMessageW(info->status, SB_SETTEXTW, 1, (LPARAM)L"My Computer");
    SendMessageW(info->toolbar, TB_CHECKBUTTON, FOLDERS_BUTTON, info->show_folders);
    taskpane_update(info);
}

void browser_navigation_state(explorer_info *info, BOOL navigating)
{
    info->navigating = navigating;
    info->brand_frame = 0;
    if (info->brand) InvalidateRect(info->brand, NULL, FALSE);
    browser_update_ui(info);
}

/* ---- commands ---- */

static void show_views_menu(explorer_info *info)
{
    HMENU menu = CreatePopupMenu();
    RECT rc;
    POINT pt;

    AppendMenuW(menu, MF_STRING, IDM_VIEW_THUMBNAILS, L"Th&umbnails");
    AppendMenuW(menu, MF_STRING, IDM_VIEW_TILES, L"Tile&s");
    AppendMenuW(menu, MF_STRING, IDM_VIEW_ICONS, L"Ico&ns");
    AppendMenuW(menu, MF_STRING, IDM_VIEW_LIST, L"Lis&t");
    AppendMenuW(menu, MF_STRING, IDM_VIEW_DETAILS, L"&Details");
    browser_init_menu_popup(info, menu);
    SendMessageW(info->toolbar, TB_GETRECT, VIEWS_BUTTON, (LPARAM)&rc);
    pt.x = rc.left; pt.y = rc.bottom;
    ClientToScreen(info->toolbar, &pt);
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, info->main_window, NULL);
    DestroyMenu(menu);
}

LRESULT browser_toolbar_notify(explorer_info *info, NMHDR *nm)
{
    if (nm->hwndFrom == info->toolbar)
    {
        if (nm->code == TBN_DROPDOWN && ((NMTOOLBARW *)nm)->iItem == VIEWS_BUTTON)
        {
            show_views_menu(info);
            return TBDDRET_DEFAULT;
        }
        if (nm->code == TBN_GETINFOTIPW) return 0;
        if (nm->code == TTN_GETDISPINFOW)
        {
            static const struct { UINT id; const WCHAR *text; } tips[] =
            {
                { BACK_BUTTON, L"Back" }, { FORWARD_BUTTON, L"Forward" }, { UP_BUTTON, L"Up" }, { SEARCH_BUTTON, L"Search" },
                { FOLDERS_BUTTON, L"Folders" }, { VIEWS_BUTTON, L"Views" }
            };
            NMTTDISPINFOW *tip = (NMTTDISPINFOW *)nm;
            unsigned int i;
            for (i = 0; i < ARRAY_SIZE(tips); i++)
                if (tips[i].id == nm->idFrom) { tip->lpszText = (WCHAR *)tips[i].text; break; }
        }
    }
    return 0;
}

static void relayout(explorer_info *info);

static void toggle_folders(explorer_info *info)
{
    info->show_folders = !info->show_folders;
    IExplorerBrowser_SetOptions(info->browser, info->show_folders ? EBO_SHOWFRAMES : EBO_NONE);
    relayout(info);
    browser_update_ui(info);
}

static void relayout(explorer_info *info)
{
    RECT rc;
    GetClientRect(info->main_window, &rc);
    SendMessageW(info->main_window, WM_SIZE, 0, MAKELONG(rc.right, rc.bottom));
}

void browser_command(explorer_info *info, UINT id)
{
    switch (id)
    {
    case BACK_BUTTON:
    case IDM_GO_BACK:
        IExplorerBrowser_BrowseToObject(info->browser, NULL, SBSP_NAVIGATEBACK);
        break;
    case FORWARD_BUTTON:
    case IDM_GO_FORWARD:
        IExplorerBrowser_BrowseToObject(info->browser, NULL, SBSP_NAVIGATEFORWARD);
        break;
    case UP_BUTTON:
    case IDM_GO_UP:
        IExplorerBrowser_BrowseToObject(info->browser, NULL, SBSP_PARENT);
        break;
    case FOLDERS_BUTTON:
    case IDM_VIEW_FOLDERS:
        toggle_folders(info);
        break;
    case SEARCH_BUTTON:
        break;  /* no search companion on this system */
    case GO_BUTTON:
    {
        WCHAR text[MAX_PATH];
        HWND edit = (HWND)SendMessageW(info->path_box, CBEM_GETEDITCONTROL, 0, 0);
        LPITEMIDLIST pidl;

        if (edit && GetWindowTextW(edit, text, ARRAY_SIZE(text)) && (pidl = ILCreateFromPathW(text)))
        {
            IExplorerBrowser_BrowseToIDList(info->browser, pidl, SBSP_ABSOLUTE);
            ILFree(pidl);
        }
        else if (text[0]) ShellExecuteW(info->main_window, L"open", text, NULL, NULL, SW_SHOWNORMAL);
        break;
    }
    case IDM_FILE_NEWFOLDER: new_folder(info); break;
    case IDM_FILE_DELETE: invoke_verb(info, SVGIO_SELECTION, "delete"); break;
    case IDM_FILE_RENAME: invoke_verb(info, SVGIO_SELECTION, "rename"); break;
    case IDM_FILE_PROPERTIES: invoke_verb(info, SVGIO_SELECTION, "properties"); break;
    case IDM_FILE_CLOSE: PostMessageW(info->main_window, WM_CLOSE, 0, 0); break;
    case IDM_EDIT_CUT: invoke_verb(info, SVGIO_SELECTION, "cut"); break;
    case IDM_EDIT_COPY: invoke_verb(info, SVGIO_SELECTION, "copy"); break;
    case IDM_EDIT_PASTE: invoke_verb(info, SVGIO_BACKGROUND, "paste"); break;
    case IDM_EDIT_SELECTALL: select_all(info); break;
    case IDM_EDIT_INVERT: invert_selection(info); break;
    case IDM_VIEW_STANDARD:
    case IDM_VIEW_ADDRESS:
    {
        BOOL *flag = id == IDM_VIEW_STANDARD ? &info->show_toolbar : &info->show_address;
        UINT band = id == IDM_VIEW_STANDARD ? 0 : 2;
        *flag = !*flag;
        SendMessageW(info->rebar, RB_SHOWBAND, band, *flag);
        if (id == IDM_VIEW_STANDARD) SendMessageW(info->rebar, RB_SHOWBAND, 1, *flag);
        relayout(info);
        break;
    }
    case IDM_VIEW_STATUS:
        info->show_status = !info->show_status;
        relayout(info);
        break;
    case IDM_VIEW_THUMBNAILS: set_view_mode(info, FVM_THUMBNAIL); break;
    case IDM_VIEW_TILES: set_view_mode(info, FVM_TILE); break;
    case IDM_VIEW_ICONS: set_view_mode(info, FVM_ICON); break;
    case IDM_VIEW_LIST: set_view_mode(info, FVM_LIST); break;
    case IDM_VIEW_DETAILS: set_view_mode(info, FVM_DETAILS); break;
    case IDM_VIEW_REFRESH:
    {
        IShellView *sv = current_view(info);
        if (sv) { IShellView_Refresh(sv); IShellView_Release(sv); }
        break;
    }
    case IDM_HELP_ABOUT:
        ShellExecuteW(info->main_window, L"open", L"winver.exe", NULL, NULL, SW_SHOWNORMAL);
        break;
    default:
        if (id >= IDM_FAVORITES_FIRST && id < IDM_FAVORITES_FIRST + 100) open_favorite(info, id - IDM_FAVORITES_FIRST);
        break;
    }
}
