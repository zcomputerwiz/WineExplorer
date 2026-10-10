/*
 * WineExplorer Desktop Environment - taskbar toolbars
 *
 * Quick Launch, Address, Links and Desktop toolbars.  Folder toolbars list the files of a folder as icon
 * buttons (with an overflow chevron); the Address toolbar is an edit box that runs what is typed into it.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include "theme_engine.h"
#include "taskbar_props.h"
#include "toolbars.h"
#include "shellfolders.h"
#include "shellres.h"
#include "instant_search.h"

#define TB_CLASS        L"WineTaskbarToolbar"
static BOOL tb_locked(void);

static int gripper_w(void)
{
    if (tb_locked()) return 1;         /* a locked taskbar has no grippers: the bands start right after each other */
    return IsThemeActive() ? 9 : 12;   /* the classic gripper is a 3 pixel raised bar with room around it */
}
#define GRIPPER_W       gripper_w()
#define BUTTON_W        23    /* 16 pixel icon with 3 pixels either side, like a Luna toolbar button */
#define CHEVRON_W       13
#define MAX_ITEMS       64
#define IDC_ADDRESS     1
#define IDC_GO          2
#define GO_W            (tb->go ? shellres_go_width(tb->go) : 50)   /* needs tb in scope */
#define IDC_SEARCHBOX   3
#define SEARCH_ICON_W   20    /* the magnifier in front of the search box */
#define IDM_CLOSE       1
#define IDM_SHOWTEXT    2
#define IDM_OPENFOLDER  3
#define IDM_REFRESH     4
#define IDM_OVERFLOW    100
#define MAX_ITEMS_OVERFLOW 64

typedef struct _TB_ITEM
{
    WCHAR path[MAX_PATH];   /* empty: Show Desktop (or a special folder) */
    int csidl;              /* special folder of the Desktop toolbar, else -1 */
    WCHAR name[MAX_PATH];
    HICON icon;
    int x, w;               /* position inside the toolbar, when it fits */
} TB_ITEM;

typedef struct _TOOLBAR
{
    int kind;
    HWND hwnd;
    HWND tooltip;
    HWND combo, go;         /* address toolbar */
    HWND edit;              /* search toolbar */
    HICON search_icon;
    TB_ITEM items[MAX_ITEMS];
    int count;
    int visible;            /* items that fit; the rest are in the chevron menu */
    int hot, down;
    BOOL tracking, chevron_hot, chevron_down;
    BOOL show_text;
} TOOLBAR;

static TOOLBAR g_tb[TOOLBAR_COUNT];
static BOOL g_all_minimized;

static const WCHAR *const tb_fixed_values[4] = { L"QuickLaunch", L"Address", L"Links", L"Desktop" };
static const WCHAR *const tb_fixed_names[4] = { L"Quick Launch", L"Address", L"Links", L"Desktop" };
static WCHAR g_custom_dir[TOOLBAR_CUSTOM_MAX][MAX_PATH];
#define IS_CUSTOM(kind) ((kind) >= TOOLBAR_CUSTOM && (kind) < TOOLBAR_CUSTOM + TOOLBAR_CUSTOM_MAX)
/* toolbars made of an edit box rather than buttons */
#define IS_BOX(kind) ((kind) == TOOLBAR_ADDRESS || (kind) == TOOLBAR_SEARCH)

/* bands: the order of the toolbars and the task buttons (TASK_BAND) along the taskbar, and the width of each */
#define TASK_BAND TOOLBAR_COUNT
static int g_order[TOOLBAR_COUNT + 1];
static int g_width[TOOLBAR_COUNT];
static BOOL g_order_loaded;

static const WCHAR *tb_name(int kind)
{
    static WCHAR names[TOOLBAR_CUSTOM_MAX][MAX_PATH];

    if (kind < TOOLBAR_CUSTOM) return tb_fixed_names[kind];
    if (kind == TOOLBAR_SEARCH) return L"Search";
    lstrcpynW(names[kind - TOOLBAR_CUSTOM], PathFindFileNameW(g_custom_dir[kind - TOOLBAR_CUSTOM]), MAX_PATH);
    return names[kind - TOOLBAR_CUSTOM];
}

static void tb_value(int kind, WCHAR *out)
{
    if (kind < TOOLBAR_CUSTOM) lstrcpyW(out, tb_fixed_values[kind]);
    else if (kind == TOOLBAR_SEARCH) lstrcpyW(out, L"Search");
    else swprintf(out, 16, L"Custom%d", kind - TOOLBAR_CUSTOM);
}
static const WCHAR tb_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Taskbar\\Toolbars";

/* ---- Show Desktop ---- */

typedef struct _MINIMIZE_ENUM_DATA
{
    HWND windows[256];
    int count;
    BOOL any_restored;
} MINIMIZE_ENUM_DATA;

static BOOL CALLBACK enum_windows_for_minimize(HWND hwnd, LPARAM lparam)
{
    MINIMIZE_ENUM_DATA *data = (MINIMIZE_ENUM_DATA *)lparam;
    DWORD style = GetWindowLongW(hwnd, GWL_STYLE), ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    WCHAR cls[64];

    if (!IsWindowVisible(hwnd) || (ex & WS_EX_TOOLWINDOW) || GetWindow(hwnd, GW_OWNER)) return TRUE;
    if (!(style & WS_MINIMIZEBOX) && !(style & WS_CAPTION)) return TRUE;
    if (GetClassNameW(hwnd, cls, ARRAY_SIZE(cls)) && (!wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Progman"))) return TRUE;
    if (data->count < 256)
    {
        data->windows[data->count++] = hwnd;
        if (!IsIconic(hwnd)) data->any_restored = TRUE;
    }
    return TRUE;
}

BOOL toolbars_desktop_shown(void)
{
    return g_all_minimized;
}

void show_desktop_toggle(void)
{
    MINIMIZE_ENUM_DATA data = {0};
    int i;

    EnumWindows(enum_windows_for_minimize, (LPARAM)&data);
    if (data.any_restored && !g_all_minimized)
    {
        for (i = 0; i < data.count; i++) if (!IsIconic(data.windows[i])) ShowWindow(data.windows[i], SW_MINIMIZE);
        g_all_minimized = TRUE;
    }
    else
    {
        for (i = 0; i < data.count; i++) if (IsIconic(data.windows[i])) ShowWindow(data.windows[i], SW_RESTORE);
        g_all_minimized = FALSE;
    }
}

/* ---- item lists ---- */

static int compare_items(const void *a, const void *b)
{
    return wcsicmp(((const TB_ITEM *)a)->name, ((const TB_ITEM *)b)->name);
}

static void add_item(TOOLBAR *tb, const WCHAR *path, const WCHAR *name, HICON icon)
{
    TB_ITEM *item;
    if (tb->count >= MAX_ITEMS) { if (icon) DestroyIcon(icon); return; }
    item = &tb->items[tb->count++];
    memset(item, 0, sizeof(*item));
    lstrcpynW(item->path, path, MAX_PATH);
    lstrcpynW(item->name, name, MAX_PATH);
    item->icon = icon;
    item->csidl = -1;
}

static void add_folder_items(TOOLBAR *tb, const WCHAR *dir)
{
    WCHAR pattern[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE find;
    int first = tb->count;

    swprintf(pattern, MAX_PATH, L"%s\\*", dir);
    if ((find = FindFirstFileW(pattern, &fd)) == INVALID_HANDLE_VALUE) return;
    do
    {
        SHFILEINFOW info = {0};
        WCHAR path[MAX_PATH];

        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) || !wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..") ||
            !wcsicmp(fd.cFileName, L"desktop.ini") || !wcsicmp(fd.cFileName, L"Show Desktop.scf"))
            continue;
        swprintf(path, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
        SHGetFileInfoW(path, fd.dwFileAttributes, &info, sizeof(info), SHGFI_ICON | SHGFI_SMALLICON | SHGFI_DISPLAYNAME);
        add_item(tb, path, info.szDisplayName[0] ? info.szDisplayName : fd.cFileName, info.hIcon);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    qsort(&tb->items[first], tb->count - first, sizeof(TB_ITEM), compare_items);
}

static BOOL toolbar_folder(int kind, WCHAR *dir)
{
    WCHAR base[MAX_PATH];

    switch (kind)
    {
    case TOOLBAR_QUICKLAUNCH:
        if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, base))) return FALSE;
        swprintf(dir, MAX_PATH, L"%s\\Microsoft\\Internet Explorer\\Quick Launch", base);
        return TRUE;
    case TOOLBAR_LINKS:
        if (FAILED(SHGetFolderPathW(NULL, CSIDL_FAVORITES, NULL, SHGFP_TYPE_CURRENT, base))) return FALSE;
        swprintf(dir, MAX_PATH, L"%s\\Links", base);
        return TRUE;
    case TOOLBAR_DESKTOP:
        return SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, dir));
    }
    if (IS_CUSTOM(kind) && g_custom_dir[kind - TOOLBAR_CUSTOM][0])
    {
        lstrcpynW(dir, g_custom_dir[kind - TOOLBAR_CUSTOM], MAX_PATH);
        return TRUE;
    }
    return FALSE;
}

static void free_items(TOOLBAR *tb)
{
    int i;
    for (i = 0; i < tb->count; i++) if (tb->items[i].icon) DestroyIcon(tb->items[i].icon);
    tb->count = 0;
}

static void load_items(TOOLBAR *tb)
{
    WCHAR dir[MAX_PATH];

    free_items(tb);
    switch (tb->kind)
    {
    case TOOLBAR_QUICKLAUNCH:
        add_item(tb, L"", L"Show Desktop", shellres_module_icon(L"explorer", 103, 16));
        if (toolbar_folder(tb->kind, dir)) add_folder_items(tb, dir);
        if (tb->count == 1)
        {
            /* a fresh profile: "Launch Internet Explorer Browser", like the default Quick Launch of Windows */
        {
            HICON icon = shellres_app_icon(L"iexplore.exe", 16);
            add_item(tb, L"iexplore.exe", L"Launch Internet Explorer Browser", icon ? icon : shellres_icon(3, 16));
        }
        }
        break;
    case TOOLBAR_LINKS:
    case TOOLBAR_CUSTOM:
    case TOOLBAR_CUSTOM + 1:
    case TOOLBAR_CUSTOM + 2:
    case TOOLBAR_CUSTOM + 3:
        if (toolbar_folder(tb->kind, dir)) add_folder_items(tb, dir);
        break;
    case TOOLBAR_DESKTOP:
    {
        /* the same items as the desktop */
        int csidls[8], n = shellfolder_desktop_items(csidls, ARRAY_SIZE(csidls)), i;
        WCHAR name[MAX_PATH];

        for (i = 0; i < n; i++)
        {
            if (!shellfolder_name(csidls[i], name, ARRAY_SIZE(name))) continue;
            int before = tb->count;
            add_item(tb, L"", name, shellfolder_icon(csidls[i], csidls[i] == CSIDL_BITBUCKET ? L"empty" : NULL, 16));
            if (tb->count > before) tb->items[before].csidl = csidls[i];
        }
    }
        if (toolbar_folder(tb->kind, dir)) add_folder_items(tb, dir);
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, dir))) add_folder_items(tb, dir);
        break;
    }
}

/* ---- layout ---- */

static BOOL tb_locked(void)
{
    TASKBAR_SETTINGS *s = taskbar_settings_get();
    return s && s->lock_taskbar;
}

static int item_width(HDC hdc, const TB_ITEM *item, BOOL show_text)
{
    SIZE size;
    if (!show_text) return BUTTON_W;
    GetTextExtentPoint32W(hdc, item->name, lstrlenW(item->name), &size);
    return min(3 + 16 + 4 + size.cx + 6, 140);
}

/* Toolbars other than Quick Launch show their name in front of the buttons, in the style's band text */
static int title_width(TOOLBAR *tb)
{
    HDC hdc;
    HFONT font, old;
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    SIZE size = {0};

    if (tb->kind == TOOLBAR_QUICKLAUNCH || tb->kind == TOOLBAR_SEARCH) return 0;   /* the search box says it inside */
    hdc = GetDC(NULL);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    font = CreateFontIndirectW(&ncm.lfMessageFont);
    old = SelectObject(hdc, font);
    GetTextExtentPoint32W(hdc, tb_name(tb->kind), lstrlenW(tb_name(tb->kind)), &size);
    SelectObject(hdc, old);
    DeleteObject(font);
    ReleaseDC(NULL, hdc);
    return size.cx + 8;
}

/* Icon toolbars: gripper and buttons.  Returns the width they want. */
static int preferred_width(TOOLBAR *tb)
{
    HDC hdc;
    HFONT font, old;
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    int i, width;

    if (tb->kind == TOOLBAR_ADDRESS) return GRIPPER_W + title_width(tb) + 190;
    if (tb->kind == TOOLBAR_SEARCH) return GRIPPER_W + SEARCH_ICON_W + 180;
    hdc = GetDC(NULL);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    font = CreateFontIndirectW(&ncm.lfMessageFont);
    old = SelectObject(hdc, font);
    for (i = 0, width = GRIPPER_W + title_width(tb); i < tb->count; i++) width += item_width(hdc, &tb->items[i], tb->show_text);
    if (!IsThemeActive()) width += 2;
    SelectObject(hdc, old);
    DeleteObject(font);
    ReleaseDC(NULL, hdc);
    return width;
}

/* Assign x positions for the current client width and work out which items overflow into the chevron */
static void place_items(TOOLBAR *tb)
{
    RECT rc;
    HDC hdc = GetDC(tb->hwnd);
    HFONT font, old;
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    int i, x = GRIPPER_W + title_width(tb);

    GetClientRect(tb->hwnd, &rc);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    font = CreateFontIndirectW(&ncm.lfMessageFont);
    old = SelectObject(hdc, font);
    tb->visible = 0;
    for (i = 0; i < tb->count; i++)
    {
        int w = item_width(hdc, &tb->items[i], tb->show_text);
        int limit = rc.right - ((i < tb->count - 1) ? CHEVRON_W : 0);
        if (x + w > limit) break;
        tb->items[i].x = x;
        tb->items[i].w = w;
        x += w;
        tb->visible++;
    }
    SelectObject(hdc, old);
    DeleteObject(font);
    ReleaseDC(tb->hwnd, hdc);
}

static RECT item_rect(const TOOLBAR *tb, int i)
{
    RECT rc;
    GetClientRect(tb->hwnd, &rc);
    rc.left = tb->items[i].x;
    rc.right = rc.left + tb->items[i].w;
    rc.top += 3;
    rc.bottom = min(rc.bottom, theme_taskbar_first_row()) - 3;   /* a taller taskbar keeps the buttons in the first row */
    return rc;
}

static RECT chevron_rect(const TOOLBAR *tb)
{
    RECT rc;
    GetClientRect(tb->hwnd, &rc);
    rc.left = rc.right - CHEVRON_W;
    rc.top += 3;
    rc.bottom = min(rc.bottom, theme_taskbar_first_row()) - 3;
    return rc;
}

static BOOL has_chevron(const TOOLBAR *tb)
{
    return !IS_BOX(tb->kind) && tb->visible < tb->count;
}

static int hit_test(const TOOLBAR *tb, int x, int y)
{
    int i;
    POINT pt = { x, y };
    for (i = 0; i < tb->visible; i++)
    {
        RECT rc = item_rect(tb, i);
        if (PtInRect(&rc, pt)) return i;
    }
    return -1;
}

/* ---- drawing ---- */

static void draw_show_desktop(HDC hdc, const RECT *rc)
{
    RECT sky = { rc->left, rc->top, rc->left + 14, rc->top + 10 }, desk;

    FillRect(hdc, &sky, GetSysColorBrush(COLOR_BACKGROUND));
    FrameRect(hdc, &sky, GetSysColorBrush(COLOR_BTNSHADOW));
    desk = sky;
    InflateRect(&desk, -2, -2);
    desk.bottom -= 2;
    FillRect(hdc, &desk, GetSysColorBrush(COLOR_WINDOW));
    desk.bottom = desk.top + 2;
    FillRect(hdc, &desk, GetSysColorBrush(COLOR_ACTIVECAPTION));
}

static void paint_toolbar(TOOLBAR *tb, HDC hdc)
{
    RECT rc, grip;
    HTHEME rebar = theme_open(tb->hwnd, L"TaskBar::Rebar");
    HTHEME buttons = theme_open(tb->hwnd, L"TaskBar::Toolbar");
    HFONT font, old_font;
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    int i;

    GetClientRect(tb->hwnd, &rc);
    DrawThemeParentBackground(tb->hwnd, hdc, NULL);   /* the taskbar under the band */

    /* a locked taskbar only shows a thin separator, an unlocked one the gripper handle */
    grip = rc;
    grip.left += 1; grip.right = grip.left + 6; grip.top += 2; grip.bottom -= 2;
    if (tb_locked())
    {
        /* nothing: Windows draws no handle and no separator on a locked taskbar */
    }
    else if (rebar) DrawThemeBackground(rebar, hdc, RP_GRIPPER, 0, &grip, NULL);
    else
    {
        /* Windows Classic: a raised bar, 3 pixels wide and 18 high */
        RECT bar = { rc.left + 1, rc.top + 6, rc.left + 4, rc.top + 24 };
        DrawEdge(hdc, &bar, BDR_RAISEDINNER, BF_RECT);
    }

    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    font = CreateFontIndirectW(&ncm.lfMessageFont);
    old_font = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);

    if (tb->search_icon)
    {
        RECT box;
        GetClientRect(tb->hwnd, &box);
        DrawIconEx(hdc, GRIPPER_W + 1, (min(box.bottom, theme_taskbar_first_row()) - 16) / 2, tb->search_icon, 16, 16, 0, NULL, DI_NORMAL);
    }
    if (tb->kind != TOOLBAR_QUICKLAUNCH && tb->kind != TOOLBAR_SEARCH)
    {
        RECT label = { GRIPPER_W + 2, rc.top, GRIPPER_W + title_width(tb), rc.bottom };
        COLORREF color = GetSysColor(COLOR_BTNTEXT);
        LOGFONTW lf;
        HFONT title_font = NULL, previous;

        if (rebar)
        {
            GetThemeColor(rebar, 0, 0, TMT_TEXTCOLOR, &color);
            if (SUCCEEDED(GetThemeFont(rebar, hdc, 0, 0, TMT_FONT, &lf))) title_font = CreateFontIndirectW(&lf);
        }
        previous = title_font ? SelectObject(hdc, title_font) : NULL;
        SetTextColor(hdc, color);
        DrawTextW(hdc, tb_name(tb->kind), -1, &label, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        if (previous) SelectObject(hdc, previous);
        if (title_font) DeleteObject(title_font);
    }
    for (i = 0; i < tb->visible; i++)
    {
        RECT item = item_rect(tb, i);
        int state = (i == tb->down && i == tb->hot) ? 3 : (i == tb->hot ? 2 : 1), off = state == 3 ? 1 : 0;
        RECT text;

        if (state > 1)
        {
            if (buttons) DrawThemeBackground(buttons, hdc, TP_BUTTON, state, &item, NULL);
            else DrawEdge(hdc, &item, state == 3 ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
        }
        if (tb->items[i].icon)
            DrawIconEx(hdc, item.left + 3 + off, item.top + (item.bottom - item.top - 16) / 2 + off, tb->items[i].icon, 16, 16, 0, NULL, DI_NORMAL);
        else
        {
            RECT glyph = { item.left + 4 + off, item.top + 4 + off, item.left + 18 + off, item.top + 16 + off };
            draw_show_desktop(hdc, &glyph);
        }
        if (tb->show_text)
        {
            text = item;
            text.left += 3 + 16 + 4 + off; text.right -= 3; text.top += off;
            SetTextColor(hdc, buttons ? RGB(255, 255, 255) : GetSysColor(COLOR_BTNTEXT));
            DrawTextW(hdc, tb->items[i].name, -1, &text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
    }
    if (has_chevron(tb))
    {
        RECT chev = chevron_rect(tb);
        int state = tb->chevron_down ? 3 : (tb->chevron_hot ? 2 : 1);
        if (rebar)
            DrawThemeBackground(rebar, hdc, RP_CHEVRON, state, &chev, NULL);
        else
        {
            if (tb->chevron_hot || tb->chevron_down) DrawEdge(hdc, &chev, tb->chevron_down ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
            SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
            DrawTextW(hdc, L"\xbb", -1, &chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
        }
    }
    SelectObject(hdc, old_font);
    DeleteObject(font);
    if (rebar) CloseThemeData(rebar);
    if (buttons) CloseThemeData(buttons);
}

/* ---- actions ---- */

static void activate_item(TOOLBAR *tb, int i)
{
    TB_ITEM *item = &tb->items[i];

    if (item->csidl >= 0) shellfolder_open(item->csidl, FALSE);
    else if (!item->path[0]) show_desktop_toggle();
    else ShellExecuteW(NULL, NULL, item->path, NULL, NULL, SW_SHOWNORMAL);
}

static void show_overflow_menu(TOOLBAR *tb)
{
    HMENU menu = CreatePopupMenu();
    RECT rc = chevron_rect(tb);
    POINT pt = { rc.right, rc.bottom };
    UINT cmd;
    int i;

    HBITMAP bitmaps[MAX_ITEMS_OVERFLOW] = { 0 };

    for (i = tb->visible; i < tb->count; i++)
    {
        MENUITEMINFOW mii = { sizeof(mii), MIIM_ID | MIIM_STRING | MIIM_FTYPE };

        mii.fType = MFT_STRING;
        mii.wID = IDM_OVERFLOW + i;
        mii.dwTypeData = tb->items[i].name;
        if (tb->items[i].icon && i < MAX_ITEMS_OVERFLOW)
        {
            HDC screen = GetDC(NULL), dc = CreateCompatibleDC(screen);
            HBITMAP old;
            RECT r = { 0, 0, 20, 16 };   /* room between the picture and the name */

            bitmaps[i] = CreateCompatibleBitmap(screen, 20, 16);
            old = SelectObject(dc, bitmaps[i]);
            FillRect(dc, &r, GetSysColorBrush(COLOR_MENU));
            DrawIconEx(dc, 0, 0, tb->items[i].icon, 16, 16, 0, NULL, DI_NORMAL);
            SelectObject(dc, old);
            DeleteDC(dc);
            ReleaseDC(NULL, screen);
            mii.fMask |= MIIM_BITMAP;
            mii.hbmpItem = bitmaps[i];
        }
        InsertMenuItemW(menu, i - tb->visible, TRUE, &mii);
    }
    ClientToScreen(tb->hwnd, &pt);
    tb->chevron_down = TRUE;
    InvalidateRect(tb->hwnd, NULL, FALSE);
    cmd = TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RETURNCMD, pt.x, pt.y - 3, 0, tb->hwnd, NULL);
    DestroyMenu(menu);
    for (i = 0; i < MAX_ITEMS_OVERFLOW; i++) if (bitmaps[i]) DeleteObject(bitmaps[i]);
    tb->chevron_down = FALSE;
    InvalidateRect(tb->hwnd, NULL, FALSE);
    if (cmd >= IDM_OVERFLOW) activate_item(tb, cmd - IDM_OVERFLOW);
}

static void run_address(TOOLBAR *tb)
{
    WCHAR text[1024];
    HKEY key;
    WCHAR name[16];
    int i;

    GetWindowTextW(tb->combo, text, ARRAY_SIZE(text));
    if (!text[0]) return;

    /* remember it (newest first), like the typed URL list */
    if (!RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Internet Explorer\\TypedURLs", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key, NULL))
    {
        WCHAR old[1024];
        DWORD size;
        for (i = 24; i >= 1; i--)
        {
            swprintf(name, ARRAY_SIZE(name), L"url%d", i);
            size = sizeof(old);
            if (!RegQueryValueExW(key, name, NULL, NULL, (BYTE *)old, &size))
            {
                swprintf(name, ARRAY_SIZE(name), L"url%d", i + 1);
                RegSetValueExW(key, name, 0, REG_SZ, (BYTE *)old, size);
            }
        }
        RegSetValueExW(key, L"url1", 0, REG_SZ, (BYTE *)text, (lstrlenW(text) + 1) * sizeof(WCHAR));
        RegCloseKey(key);
    }
    ShellExecuteW(NULL, L"open", text, NULL, NULL, SW_SHOWNORMAL);
    SetWindowTextW(tb->combo, L"");
}

static void load_address_history(TOOLBAR *tb)
{
    HKEY key;
    WCHAR name[16], value[1024];
    DWORD size;
    int i;

    SendMessageW(tb->combo, CB_RESETCONTENT, 0, 0);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Internet Explorer\\TypedURLs", 0, KEY_READ, &key)) return;
    for (i = 1; i <= 25; i++)
    {
        swprintf(name, ARRAY_SIZE(name), L"url%d", i);
        size = sizeof(value);
        if (!RegQueryValueExW(key, name, NULL, NULL, (BYTE *)value, &size)) SendMessageW(tb->combo, CB_ADDSTRING, 0, (LPARAM)value);
    }
    RegCloseKey(key);
}

static void show_context_menu(TOOLBAR *tb, POINT screen)
{
    HMENU menu = CreatePopupMenu();
    WCHAR dir[MAX_PATH];
    BOOL folder = toolbar_folder(tb->kind, dir) && !IS_BOX(tb->kind);
    UINT cmd;

    if (!IS_BOX(tb->kind))
        AppendMenuW(menu, MF_STRING | (tb->show_text ? MF_CHECKED : 0), IDM_SHOWTEXT, L"Show &Text");
    if (folder)
    {
        AppendMenuW(menu, MF_STRING, IDM_OPENFOLDER, L"&Open Folder");
        AppendMenuW(menu, MF_STRING, IDM_REFRESH, L"&Refresh");
    }
    if (GetMenuItemCount(menu) > 0) AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_CLOSE, L"&Close Toolbar");
    cmd = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, screen.x, screen.y, 0, tb->hwnd, NULL);
    DestroyMenu(menu);

    switch (cmd)
    {
    case IDM_SHOWTEXT:
    {
        HKEY key;
        DWORD v;
        tb->show_text = !tb->show_text;
        if (!RegCreateKeyExW(HKEY_CURRENT_USER, tb_key, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL))
        {
            WCHAR name[64];
            WCHAR value[16];
            tb_value(tb->kind, value);
            swprintf(name, ARRAY_SIZE(name), L"%sText", value);
            v = tb->show_text;
            RegSetValueExW(key, name, 0, REG_DWORD, (BYTE *)&v, sizeof(v));
            RegCloseKey(key);
        }
        SendMessageW(GetParent(tb->hwnd), WM_SETTINGCHANGE, 0, 0);
        break;
    }
    case IDM_OPENFOLDER:
        ShellExecuteW(NULL, L"open", L"explorer.exe", dir, NULL, SW_SHOWNORMAL);
        break;
    case IDM_REFRESH:
        toolbars_refresh();
        break;
    case IDM_CLOSE:
        toolbars_set_visible(GetParent(tb->hwnd), tb->kind, FALSE);
        break;
    }
}

static void update_tooltips(TOOLBAR *tb)
{
    TOOLINFOW ti = { sizeof(ti) };
    int i;

    if (!tb->tooltip) return;
    for (i = 0; i < MAX_ITEMS; i++)
    {
        ti.hwnd = tb->hwnd; ti.uId = i;
        SendMessageW(tb->tooltip, TTM_DELTOOLW, 0, (LPARAM)&ti);
    }
    for (i = 0; i < tb->visible && !IS_BOX(tb->kind); i++)
    {
        ti.uFlags = TTF_SUBCLASS; ti.hwnd = tb->hwnd; ti.uId = i;
        ti.rect = item_rect(tb, i);
        ti.lpszText = tb->items[i].name;
        SendMessageW(tb->tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
}

static TOOLBAR *toolbar_from_hwnd(HWND hwnd)
{
    int i;
    for (i = 0; i < TOOLBAR_COUNT; i++) if (g_tb[i].hwnd == hwnd) return &g_tb[i];
    return NULL;
}

static void position_combo(TOOLBAR *tb)
{
    RECT rc;
    if (!tb->combo) return;
    GetClientRect(tb->hwnd, &rc);
    SetWindowPos(tb->combo, NULL, GRIPPER_W + title_width(tb) + 2, (rc.bottom - 22) / 2, max(20, rc.right - GRIPPER_W - title_width(tb) - 2 - GO_W - 4), 200,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(tb->go, NULL, rc.right - GO_W - 2, (rc.bottom - 22) / 2, GO_W, 22, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetWindowPos(tb->combo, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    RedrawWindow(tb->hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
    RedrawWindow(tb->combo, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW | RDW_ALLCHILDREN);
    RedrawWindow(tb->go, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
}

static void position_search(TOOLBAR *tb)
{
    RECT rc;
    int h = 20, top;

    if (!tb->edit) return;
    GetClientRect(tb->hwnd, &rc);
    top = (min(rc.bottom, theme_taskbar_first_row()) - h) / 2;
    SetWindowPos(tb->edit, NULL, GRIPPER_W + SEARCH_ICON_W, top, max(20, rc.right - GRIPPER_W - SEARCH_ICON_W - 4), h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

static void save_bands(void);
static int g_drag_kind = -1, g_drag_start, g_drag_width;
static int g_drag_previous, g_drag_previous_width;
void toolbars_task_rect(int *x, int *w);

static int band_index(int band)
{
    int i;
    for (i = 0; i <= TOOLBAR_COUNT; i++) if (g_order[i] == band) return i;
    return -1;
}

static int minimum_band_width(TOOLBAR *tb)
{
    if (tb->kind == TOOLBAR_SEARCH) return GRIPPER_W + SEARCH_ICON_W + 24;
    if (tb->kind == TOOLBAR_ADDRESS) return GRIPPER_W + title_width(tb) + GO_W + 26;
    return GRIPPER_W + CHEVRON_W + 2;
}

static void band_rect(int kind, HWND tray, RECT *rc)
{
    if (kind == TASK_BAND)
    {
        int x, w;
        toolbars_task_rect(&x, &w);
        SetRect(rc, x - GRIPPER_W, 0, x + w, 1);
        MapWindowPoints(tray, NULL, (POINT *)rc, 2);
    }
    else GetWindowRect(g_tb[kind].hwnd, rc);
}

static void begin_band_drag(int kind, HWND tray, int screen_x)
{
    RECT rc;
    int i, at = band_index(kind) - 1;

    /* Layout may have compressed saved widths to fit the screen. Start from
     * the visible widths so another band cannot reclaim that space mid-drag. */
    for (i = 0; i < TOOLBAR_COUNT; ++i)
        if (g_tb[i].hwnd)
        {
            GetClientRect(g_tb[i].hwnd, &rc);
            g_width[i] = rc.right;
        }

    g_drag_start = screen_x;
    band_rect(kind, tray, &rc);
    g_drag_width = rc.right - rc.left;
    while (at >= 0 && g_order[at] != TASK_BAND && !g_tb[g_order[at]].hwnd) --at;
    g_drag_previous = at >= 0 ? g_order[at] : -1;
    g_drag_previous_width = 0;
    if (g_drag_previous >= 0 && g_drag_previous != TASK_BAND)
    {
        GetClientRect(g_tb[g_drag_previous].hwnd, &rc);
        g_drag_previous_width = rc.right;
    }
}

/* Moving a gripper: the band widens or narrows by what the mouse moved, and changes place with a neighbor once the
 * pointer passes its middle. */
static void drag_band(int kind, HWND tray, int screen_x)
{
    int dx = screen_x - g_drag_start, pos = band_index(kind), step, other;
    RECT rc;
    int min = kind == TASK_BAND ? 120 : minimum_band_width(&g_tb[kind]);

    if (pos < 0) return;
    for (step = -1; step <= 1; step += 2)
    {
        int at = pos + step, w;
        while (at >= 0 && at <= TOOLBAR_COUNT && g_order[at] != TASK_BAND && !g_tb[g_order[at]].hwnd) at += step;
        if (at < 0 || at > TOOLBAR_COUNT) continue;
        other = g_order[at];
        band_rect(other, tray, &rc);
        w = rc.right - rc.left;
        if ((step < 0 && screen_x < rc.left + w / 2 - 4 && other != TASK_BAND) ||
            (step > 0 && screen_x > rc.left + w / 2 + 4 && other != TASK_BAND) ||
            (other == TASK_BAND && ((step < 0 && screen_x < rc.left + 4) || (step > 0 && screen_x > rc.right - 4))))
        {
            int tmp = g_order[pos];
            g_order[pos] = g_order[at];
            g_order[at] = tmp;
            SendMessageW(tray, WM_SETTINGCHANGE, 0, 0);
            begin_band_drag(kind, tray, screen_x);
            return;
        }
    }
    /* Move the boundary between this band and its left neighbor. The task band
     * absorbs changes through layout; other neighbors need an explicit width. */
    {
        int w = g_drag_width - dx;

        /* XP anchors the first band beside Start. Its gripper only reorders;
         * its right edge is resized with the following band's gripper. */
        if (g_drag_previous < 0) return;
        if (w < min) w = min;
        if (kind != TASK_BAND && w > 700) w = 700;
        if (g_drag_previous != TASK_BAND)
        {
            int previous_min = minimum_band_width(&g_tb[g_drag_previous]);
            int delta = g_drag_width - w;

            delta = max(delta, previous_min - g_drag_previous_width);
            w = g_drag_width - delta;
            g_width[g_drag_previous] = g_drag_previous_width + delta;
        }
        if (kind != TASK_BAND) g_width[kind] = w;
        SendMessageW(tray, WM_SETTINGCHANGE, 0, 0);
    }
}

static LRESULT CALLBACK toolbar_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    TOOLBAR *tb = toolbar_from_hwnd(hwnd);

    if (!tb && msg == WM_NCCREATE)
        tb = &g_tb[(int)(INT_PTR)((CREATESTRUCTW *)lparam)->lpCreateParams];
    if (!tb) return DefWindowProcW(hwnd, msg, wparam, lparam);

    switch (msg)
    {
    case WM_NCCREATE:
        tb->hwnd = hwnd;
        return TRUE;

    case WM_CREATE:
        tb->tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                      0, 0, 0, 0, hwnd, NULL, GetModuleHandleW(NULL), NULL);
        if (tb->kind == TOOLBAR_ADDRESS)
        {
            tb->combo = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL,
                                        0, 0, 100, 200, hwnd, (HMENU)IDC_ADDRESS, GetModuleHandleW(NULL), NULL);
            SendMessageW(tb->combo, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE);
            tb->go = shellres_go_button(hwnd, IDC_GO);
            load_address_history(tb);
        }
        else if (tb->kind == TOOLBAR_SEARCH)
        {
            tb->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 100, 20,
                                       hwnd, (HMENU)IDC_SEARCHBOX, GetModuleHandleW(NULL), NULL);
            SendMessageW(tb->edit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE);
            SendMessageW(tb->edit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Type here to search");
            tb->search_icon = shellres_icon(23, 16);
        }
        return 0;

    case WM_SIZE:
        place_items(tb);
        position_combo(tb);
        position_search(tb);
        update_tooltips(tb);
        return 0;

    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_ADDRESS && HIWORD(wparam) == CBN_DROPDOWN) load_address_history(tb);
        if (LOWORD(wparam) == IDC_GO && HIWORD(wparam) == BN_CLICKED) run_address(tb);
        if (LOWORD(wparam) == IDC_SEARCHBOX && HIWORD(wparam) == EN_CHANGE)
        {
            WCHAR text[256];
            GetWindowTextW(tb->edit, text, ARRAY_SIZE(text));
            instant_search_update(tb->edit, text);
        }
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC real = BeginPaint(hwnd, &ps), hdc;
        HPAINTBUFFER buffer = BeginBufferedPaint(real, &ps.rcPaint, BPBF_COMPATIBLEBITMAP, NULL, &hdc);

        paint_toolbar(tb, buffer ? hdc : real);
        if (buffer) EndBufferedPaint(buffer, TRUE);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_NCHITTEST:
    {
        /* Windows Classic: the top edge of an unlocked taskbar belongs to the taskbar, which changes its height from there */
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        ScreenToClient(hwnd, &pt);
        if (pt.y < 3 && !tb_locked() && !IsThemeActive()) return HTTRANSPARENT;
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEMOVE:
    {
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        BOOL chev = FALSE;
        int hot;

        if (g_drag_kind == tb->kind && GetCapture() == hwnd)
        {
            ClientToScreen(hwnd, &pt);
            drag_band(tb->kind, GetParent(hwnd), pt.x);
            return 0;
        }
        hot = hit_test(tb, pt.x, pt.y);

        if (has_chevron(tb)) { RECT c = chevron_rect(tb); chev = PtInRect(&c, pt); }
        if (!tb->tracking)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            tb->tracking = TRUE;
        }
        if (hot != tb->hot || chev != tb->chevron_hot) { tb->hot = hot; tb->chevron_hot = chev; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;
    }

    case WM_MOUSELEAVE:
        tb->tracking = FALSE;
        tb->hot = tb->down = -1;
        tb->chevron_hot = FALSE;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_SETCURSOR:
    {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        if (!tb_locked() && (pt.x < GRIPPER_W || g_drag_kind >= 0))
        {
            SetCursor(LoadCursorW(NULL, (const WCHAR *)IDC_SIZEWE));
            return TRUE;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        if (!tb_locked() && pt.x < GRIPPER_W)
        {
            POINT screen = pt;
            ClientToScreen(hwnd, &screen);
            g_drag_kind = tb->kind;
            begin_band_drag(tb->kind, GetParent(hwnd), screen.x);
            SetCapture(hwnd);
            return 0;
        }
        if (has_chevron(tb))
        {
            RECT c = chevron_rect(tb);
            if (PtInRect(&c, pt)) { show_overflow_menu(tb); return 0; }
        }
        tb->down = hit_test(tb, pt.x, pt.y);
        SetCapture(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONUP:
    {
        int i;
        int was;

        if (g_drag_kind == tb->kind)
        {
            g_drag_kind = -1;
            if (GetCapture() == hwnd) ReleaseCapture();
            save_bands();
            return 0;
        }
        i = hit_test(tb, (short)LOWORD(lparam), (short)HIWORD(lparam));
        was = tb->down;
        if (GetCapture() == hwnd) ReleaseCapture();
        tb->down = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        if (i >= 0 && i == was) activate_item(tb, i);
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (g_drag_kind == tb->kind)
        {
            g_drag_kind = -1;
            save_bands();
        }
        return 0;

    case WM_CONTEXTMENU:
    {
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        show_context_menu(tb, pt);
        return 0;
    }

    case WM_NOTIFY:
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    return 0;
}

/* The combo box eats Enter; handle it from a message hook on the parent chain by subclassing the edit */
static LRESULT CALLBACK address_edit_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR ref)
{
    if (msg == WM_KEYDOWN && wparam == VK_RETURN)
    {
        run_address(&g_tb[TOOLBAR_ADDRESS]);
        return 0;
    }
    if (msg == WM_CHAR && wparam == '\r') return 0;
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

/* The search box: arrows, Enter and Escape go to the results; it closes them when you click elsewhere */
static LRESULT CALLBACK search_edit_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR ref)
{
    switch (msg)
    {
    case WM_KEYDOWN:
        if (instant_search_key(wparam)) return 0;
        break;
    case WM_CHAR:
        if (wparam == '\r' || wparam == 27) return 0;   /* no beep */
        break;
    case WM_KILLFOCUS:
        if (!instant_search_owns((HWND)wparam))
        {
            instant_search_hide();
            SetWindowTextW(hwnd, L"");
        }
        break;
    case WM_SETFOCUS:
        if (GetWindowTextLengthW(hwnd)) PostMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(IDC_SEARCHBOX, EN_CHANGE), (LPARAM)hwnd);
        break;
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

BOOL toolbars_search_available(void)
{
    return g_tb[TOOLBAR_SEARCH].edit != NULL;
}

/* Typing in the Start menu goes on in the search box, like on Windows 10 and 11 */
void toolbars_search_type(WCHAR ch)
{
    TOOLBAR *tb = &g_tb[TOOLBAR_SEARCH];
    WCHAR text[2] = { ch, 0 };

    if (!tb->edit) return;
    SetForegroundWindow(GetParent(tb->hwnd));
    SetFocus(tb->edit);
    SetWindowTextW(tb->edit, text);   /* EN_CHANGE shows the results */
    SendMessageW(tb->edit, EM_SETSEL, 1, 1);
}

/* ---- public ---- */

static void register_class(void)
{
    WNDCLASSW wc = {0};
    if (GetClassInfoW(GetModuleHandleW(NULL), TB_CLASS, &wc)) return;
    wc.lpfnWndProc = toolbar_proc;
    wc.style = CS_HREDRAW | CS_VREDRAW;   /* everything is laid out from the size: repaint all of it */
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = TB_CLASS;
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    RegisterClassW(&wc);
}

static void read_text_flag(TOOLBAR *tb)
{
    HKEY key;
    WCHAR name[64];
    DWORD v = 0, size = sizeof(v);

    /* Links shows its text by default, like on Windows */
    tb->show_text = tb->kind == TOOLBAR_LINKS;
    {
        WCHAR value[16];
        tb_value(tb->kind, value);
        swprintf(name, ARRAY_SIZE(name), L"%sText", value);
    }
    tb->show_text = tb->kind == TOOLBAR_LINKS || IS_CUSTOM(tb->kind);
    if (!RegOpenKeyExW(HKEY_CURRENT_USER, tb_key, 0, KEY_READ, &key))
    {
        if (!RegQueryValueExW(key, name, NULL, NULL, (BYTE *)&v, &size)) tb->show_text = v != 0;
        RegCloseKey(key);
    }
}

static void create_one(HWND tray, int kind)
{
    TOOLBAR *tb = &g_tb[kind];

    if (tb->hwnd) return;
    register_class();
    memset(tb, 0, sizeof(*tb));
    tb->kind = kind;
    tb->hot = tb->down = -1;
    read_text_flag(tb);
    load_items(tb);
    tb->hwnd = CreateWindowExW(0, TB_CLASS, tb_name(kind), WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                               0, 0, preferred_width(tb), 28, tray, NULL, GetModuleHandleW(NULL), (void *)(INT_PTR)kind);
    if (kind == TOOLBAR_SEARCH && tb->edit) SetWindowSubclass(tb->edit, search_edit_proc, 1, 0);
    if (kind == TOOLBAR_ADDRESS && tb->combo)
    {
        COMBOBOXINFO info = { sizeof(info) };
        if (GetComboBoxInfo(tb->combo, &info) && info.hwndItem) SetWindowSubclass(info.hwndItem, address_edit_proc, 1, 0);
    }
}

static void destroy_one(int kind)
{
    TOOLBAR *tb = &g_tb[kind];
    if (kind == TOOLBAR_SEARCH) instant_search_hide();
    if (tb->search_icon) DestroyIcon(tb->search_icon);
    if (tb->hwnd) DestroyWindow(tb->hwnd);
    free_items(tb);
    memset(tb, 0, sizeof(*tb));
}

BOOL toolbars_is_visible(int kind)
{
    TASKBAR_SETTINGS *s = taskbar_settings_get();
    switch (kind)
    {
    case TOOLBAR_QUICKLAUNCH: return s->show_quick_launch;
    case TOOLBAR_ADDRESS: return s->show_address;
    case TOOLBAR_SEARCH: return s->show_search;
    case TOOLBAR_LINKS: return s->show_links;
    case TOOLBAR_DESKTOP: return s->show_desktop_toolbar;
    }
    return IS_CUSTOM(kind) && g_custom_dir[kind - TOOLBAR_CUSTOM][0];
}

void toolbars_set_visible(HWND tray, int kind, BOOL visible)
{
    TASKBAR_SETTINGS *s = taskbar_settings_get();

    switch (kind)
    {
    case TOOLBAR_QUICKLAUNCH: s->show_quick_launch = visible; break;
    case TOOLBAR_ADDRESS: s->show_address = visible; break;
    case TOOLBAR_SEARCH: s->show_search = visible; break;
    case TOOLBAR_LINKS: s->show_links = visible; break;
    case TOOLBAR_DESKTOP: s->show_desktop_toolbar = visible; break;
    default:
        if (!visible && IS_CUSTOM(kind))
        {
            HKEY key;
            WCHAR value[16], name[32];

            tb_value(kind, value);
            swprintf(name, ARRAY_SIZE(name), L"%sFolder", value);
            if (!RegOpenKeyExW(HKEY_CURRENT_USER, tb_key, 0, KEY_SET_VALUE, &key))
            {
                RegDeleteValueW(key, name);
                RegCloseKey(key);
            }
            g_custom_dir[kind - TOOLBAR_CUSTOM][0] = 0;
        }
        break;
    }
    taskbar_settings_save();
    if (visible) create_one(tray, kind); else destroy_one(kind);
    SendMessageW(tray, WM_SETTINGCHANGE, 0, 0);
}

static void create_one(HWND tray, int kind);

static void load_bands(void)
{
    HKEY key;
    int i;
    WCHAR name[32], order[80];
    DWORD size;

    if (g_order_loaded) return;
    g_order_loaded = TRUE;
    if (!RegOpenKeyExW(HKEY_CURRENT_USER, tb_key, 0, KEY_READ, &key))
    {
        for (i = 0; i < TOOLBAR_CUSTOM_MAX; i++)
        {
            size = sizeof(g_custom_dir[i]);
            swprintf(name, ARRAY_SIZE(name), L"Custom%dFolder", i);
            if (RegQueryValueExW(key, name, NULL, NULL, (BYTE *)g_custom_dir[i], &size)) g_custom_dir[i][0] = 0;
        }
        for (i = 0; i < TOOLBAR_COUNT; i++)
        {
            WCHAR value[16];
            DWORD w = 0;

            size = sizeof(w);
            tb_value(i, value);
            swprintf(name, ARRAY_SIZE(name), L"%sWidth", value);
            if (!RegQueryValueExW(key, name, NULL, NULL, (BYTE *)&w, &size)) g_width[i] = w;
        }
        size = sizeof(order);
        if (RegQueryValueExW(key, L"Order", NULL, NULL, (BYTE *)order, &size)) order[0] = 0;
        RegCloseKey(key);
    }
    else order[0] = 0;

    /* the saved order, then whatever is missing in the default order (Quick Launch, task buttons, the rest) */
    {
        int n = 0, seen[TOOLBAR_COUNT + 1] = { 0 };
        const WCHAR *p = order;

        /* orders saved before the search box existed: the task buttons had its number */
        BOOL old = !wcsstr(order, L",9") && wcsncmp(order, L"9", 1);

        while (*p)
        {
            int v = _wtoi(p);
            if (old && v == TOOLBAR_SEARCH) v = TASK_BAND;
            if (v >= 0 && v <= TASK_BAND && !seen[v]) { g_order[n++] = v; seen[v] = 1; }
            while (*p && *p != ',') p++;
            if (*p == ',') p++;
        }
        if (!seen[TOOLBAR_QUICKLAUNCH]) { g_order[n++] = TOOLBAR_QUICKLAUNCH; seen[TOOLBAR_QUICKLAUNCH] = 1; }
        if (!seen[TASK_BAND]) { g_order[n++] = TASK_BAND; seen[TASK_BAND] = 1; }
        for (i = 0; i < TOOLBAR_COUNT; i++) if (!seen[i]) g_order[n++] = i;
    }
}

static void save_bands(void)
{
    HKEY key;
    int i;
    WCHAR order[80] = L"", item[8];

    if (RegCreateKeyExW(HKEY_CURRENT_USER, tb_key, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL)) return;
    for (i = 0; i <= TOOLBAR_COUNT; i++)
    {
        swprintf(item, ARRAY_SIZE(item), i ? L",%d" : L"%d", g_order[i]);
        lstrcatW(order, item);
    }
    RegSetValueExW(key, L"Order", 0, REG_SZ, (BYTE *)order, (lstrlenW(order) + 1) * sizeof(WCHAR));
    for (i = 0; i < TOOLBAR_COUNT; i++)
    {
        WCHAR value[16], name[32];
        DWORD w = g_width[i];

        tb_value(i, value);
        swprintf(name, ARRAY_SIZE(name), L"%sWidth", value);
        if (w) RegSetValueExW(key, name, 0, REG_DWORD, (BYTE *)&w, sizeof(w));
    }
    RegCloseKey(key);
}

BOOL toolbars_custom_name(int kind, WCHAR *name, int len)
{
    load_bands();
    if (!IS_CUSTOM(kind) || !g_custom_dir[kind - TOOLBAR_CUSTOM][0]) return FALSE;
    lstrcpynW(name, tb_name(kind), len);
    return TRUE;
}

BOOL toolbars_add_custom(HWND tray, const WCHAR *folder)
{
    int i;

    load_bands();
    for (i = 0; i < TOOLBAR_CUSTOM_MAX; i++)
    {
        if (!g_custom_dir[i][0])
        {
            HKEY key;
            WCHAR name[32];

            lstrcpynW(g_custom_dir[i], folder, MAX_PATH);
            swprintf(name, ARRAY_SIZE(name), L"Custom%dFolder", i);
            if (!RegCreateKeyExW(HKEY_CURRENT_USER, tb_key, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL))
            {
                RegSetValueExW(key, name, 0, REG_SZ, (BYTE *)folder, (lstrlenW(folder) + 1) * sizeof(WCHAR));
                RegCloseKey(key);
            }
            create_one(tray, TOOLBAR_CUSTOM + i);
            SendMessageW(tray, WM_SETTINGCHANGE, 0, 0);
            return TRUE;
        }
    }
    return FALSE;
}

/* "New Toolbar...": pick a folder; the toolbar shows its contents */
void toolbars_choose_new(HWND tray)
{
    BROWSEINFOW bi = { 0 };
    WCHAR folder[MAX_PATH];
    LPITEMIDLIST pidl;

    bi.hwndOwner = tray;
    bi.pszDisplayName = folder;
    bi.lpszTitle = L"Choose a folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    if (!(pidl = SHBrowseForFolderW(&bi))) return;
    if (SHGetPathFromIDListW(pidl, folder)) toolbars_add_custom(tray, folder);
    CoTaskMemFree(pidl);
}

void toolbars_create(HWND tray)
{
    int kind;

    load_bands();
    for (kind = 0; kind < TOOLBAR_COUNT; kind++)
        if (toolbars_is_visible(kind)) create_one(tray, kind);
}

void toolbars_destroy(void)
{
    int kind;
    save_bands();
    for (kind = 0; kind < TOOLBAR_COUNT; kind++) destroy_one(kind);
}

void toolbars_refresh(void)
{
    int kind;
    for (kind = 0; kind < TOOLBAR_COUNT; kind++)
    {
        TOOLBAR *tb = &g_tb[kind];
        if (!tb->hwnd) continue;
        read_text_flag(tb);
        if (!IS_BOX(kind)) load_items(tb);
        place_items(tb);
        update_tooltips(tb);
        position_combo(tb);
        position_search(tb);
        RedrawWindow(tb->hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
}

static int g_task_x, g_task_w, g_task_top, g_task_height;

void toolbars_task_rect(int *x, int *w)
{
    *x = g_task_x;
    *w = g_task_w;
}

static int band_width(TOOLBAR *tb)
{
    int w = g_width[tb->kind] ? g_width[tb->kind] : preferred_width(tb);
    return max(w, minimum_band_width(tb));
}

void toolbars_layout(HWND tray, int x, int tray_left, int top, int height, int *task_x, int *task_w)
{
    int i, total = 0, avail, widths[TOOLBAR_COUNT + 1] = { 0 }, min_task = 120, cursor;
    int gap = IsThemeActive() ? 2 : 0;

    load_bands();
    g_task_top = top;
    g_task_height = height;
    avail = tray_left - x - 4;
    for (i = 0; i <= TOOLBAR_COUNT; i++)
    {
        int band = g_order[i];
        if (band == TASK_BAND || !g_tb[band].hwnd) continue;
        widths[i] = band_width(&g_tb[band]);
        total += widths[i] + gap;
    }
    /* give way: the last toolbars shrink so that the task buttons keep a minimum */
    for (i = TOOLBAR_COUNT; i >= 0 && total > avail - min_task; i--)
    {
        int band = g_order[i], give;
        if (band == TASK_BAND || !g_tb[band].hwnd) continue;
        give = min(total - (avail - min_task), max(0, widths[i] - minimum_band_width(&g_tb[band])));
        widths[i] -= give;
        total -= give;
    }
    widths[band_index(TASK_BAND)] = max(50, avail - total);

    cursor = x;
    for (i = 0; i <= TOOLBAR_COUNT; i++)
    {
        int band = g_order[i];

        if (band == TASK_BAND)
        {
            int g = tb_locked() ? 0 : gripper_w();
            g_task_x = cursor + g;
            g_task_w = widths[i] - g;
            cursor += widths[i] + gap;
        }
        else if (g_tb[band].hwnd)
        {
            SetWindowPos(g_tb[band].hwnd, NULL, cursor, top, widths[i], height, SWP_NOZORDER | SWP_NOACTIVATE);
            cursor += widths[i] + gap;
        }
    }
    *task_x = g_task_x;
    *task_w = g_task_w;
    (void)tray;
}

/* The task band's gripper is painted in the tray, outside its button window. */
BOOL toolbars_task_gripper_message(HWND tray, UINT msg, WPARAM wparam, LPARAM lparam)
{
    POINT pt;
    BOOL dragging = g_drag_kind == TASK_BAND;

    switch (msg)
    {
    case WM_SETCURSOR:
    case WM_LBUTTONDOWN:
        if (msg == WM_SETCURSOR && dragging)
        {
            SetCursor(LoadCursorW(NULL, (const WCHAR *)IDC_SIZEWE));
            return TRUE;
        }
        if (msg == WM_LBUTTONDOWN)
        {
            pt.x = (short)LOWORD(lparam);
            pt.y = (short)HIWORD(lparam);
        }
        else
        {
            GetCursorPos(&pt);
            ScreenToClient(tray, &pt);
        }
        if (tb_locked() || pt.x < g_task_x - GRIPPER_W || pt.x >= g_task_x ||
            pt.y < g_task_top + 3 || pt.y >= g_task_top + g_task_height) return FALSE;
        if (msg == WM_SETCURSOR)
            SetCursor(LoadCursorW(NULL, (const WCHAR *)IDC_SIZEWE));
        else
        {
            ClientToScreen(tray, &pt);
            g_drag_kind = TASK_BAND;
            begin_band_drag(TASK_BAND, tray, pt.x);
            SetCapture(tray);
        }
        return TRUE;
    case WM_MOUSEMOVE:
        if (!dragging || GetCapture() != tray) return FALSE;
        pt.x = (short)LOWORD(lparam);
        pt.y = (short)HIWORD(lparam);
        ClientToScreen(tray, &pt);
        drag_band(TASK_BAND, tray, pt.x);
        return TRUE;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (!dragging) return FALSE;
        g_drag_kind = -1;
        if (msg == WM_LBUTTONUP && GetCapture() == tray) ReleaseCapture();
        save_bands();
        return TRUE;
    }
    return FALSE;
}

/* The gripper in front of the task buttons, drawn by the taskbar; it is the same handle as that of the toolbars */
void toolbars_draw_task_gripper(HWND tray, HDC hdc, int top, int height)
{
    HTHEME rebar;
    RECT grip;

    if (tb_locked()) return;
    rebar = theme_open(tray, L"TaskBar::Rebar");
    SetRect(&grip, g_task_x - gripper_w() + 1, top + 2, g_task_x - gripper_w() + 7, top + height - 2);
    if (rebar)
    {
        DrawThemeBackground(rebar, hdc, RP_GRIPPER, 0, &grip, NULL);
        CloseThemeData(rebar);
    }
    else
    {
        RECT bar = { g_task_x - gripper_w() + 1, top + 6, g_task_x - gripper_w() + 4, top + 24 };
        DrawEdge(hdc, &bar, BDR_RAISEDINNER, BF_RECT);
    }
}
